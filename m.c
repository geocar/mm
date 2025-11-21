#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/prctl.h>
#include <linux/tiocl.h>
#include <linux/capability.h>
#include <sys/syscall.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <signal.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#define DEV_TTY_SIG   (SIGRTMIN+3)
#define DEV_INPUT_SIG (SIGRTMIN+2)
#define DEV_VCS_SIG   (SIGRTMIN+1)

#define C(X,Y,Z)__builtin_memcpy((void*)(X),(void*)(Y),(Z))
#define N(I,X...)for(int _=(I),n=_,i=0;i<n;++i){X;}
static struct winsize W={0};
static unsigned long long vt_mask;static int vt_mask_set=0, vt_mask_active=0;
static struct consolefontdesc desc1;
static struct console_font_op desc2;
static char stuff[512];
static unsigned char font[512*32];
static unsigned char fh,fp,fs,stuffn=0;
static int v,c,gfxp,fontx,dimn,dimx;
static int active=0;

static const unsigned char cursor[128]={0x80,0xc0,0xe0,0xf0,0xf8,0xfc,0xfe,0xff,0xfe,0xf8,0x8c,0x0c,0x06,0x06,0,0,  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
/* extra rows here are so i don't have to worry about going off the end when rendering */
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static unsigned char *volatile screen;
static unsigned char stash[8], shadow[8];
static int X(int x){return x>>3;};static int Y(int y){return y/fh;};
static int pX(int x){return x<<3;}static int pY(int y){return y*fh;};

static __attribute__((pure)) unsigned char *volatile S(int x, int y) { return screen+4+2*(W.ws_col*y+x); }

static void(*setfont)(void);
static void setfont_pio_fontx(void){ioctl(c,PIO_FONTX,&desc1);}
static void setfont_kdfontop(void){desc2.op=KD_FONT_OP_SET;ioctl(c,KDFONTOP,&desc2);}

static void W2(struct iovec*i,int n){
	int r=writev(2,i,n);
	while(r>0 && n>0){
		if(r>=i->iov_len) r-= i->iov_len,i++,n--;else i->iov_base += r,i->iov_len -=r,r=writev(2,i,n);
	}
}
static const char P[]={1,3,2,4};
static void mapvideo(void);
static void loadfont(void){
	setfont=setfont_pio_fontx;desc1.charcount=512;desc1.charheight=32;
	desc1.chardata=(void*)font;fontx=ioctl(c,GIO_FONTX,&desc1);fp=fh=desc1.charheight;
	if(fontx) {
		desc2.op = KD_FONT_OP_GET;desc2.width=8;desc2.height=32;desc2.charcount=512;desc2.data=font;
		setfont=setfont_kdfontop,fontx=ioctl(c,KDFONTOP,&desc2);
		if(desc2.width!=8)fontx=-1; // nyi?
		fh=desc2.height;fp=32;//desc2.charcount=8;
	}
	if(fh<fp)fs=fp-fh;else fs=0,fh=fp;
}
static void erasecursor(void);
static void ino(char *x, int i) {
	if(i<10)*x=i+'0',x[1]=0;
	else if(i<100)x[0]=(i/10)+'0',x[1]=(i%10)+'0',x[2]=0;
	else if(i<1000)x[0]=(i/100)+'0',x[1]=((i/10)%10)+'0',x[2]=(i%10)+'0',x[3]=0;
	else __builtin_abort();
}
static char tty_name_buf[16]={'/','d','e','v','/','t','t','y',0/*(8)*/,0,0,0,0, 0,0,0};

static int fasync(int f,int sig){
	int e;
	return ((e=fcntl(f,F_SETSIG,sig))||(e=fcntl(f,F_SETOWN,getpid()))
			||(e=fcntl(f,F_SETFL,fcntl(f,F_GETFL,0)|O_NONBLOCK|O_ASYNC)))?e:0;
}

static void openscreen(int i){
	char vcsa[16]={'/','d','e','v','/','v','c','s','a',0,0,0,0,0,0,0};

	if((vt_mask_active=(vt_mask_set&&(i<0||i>63||!(vt_mask&(1ull<<i))))))return;

	ino(tty_name_buf+8,i);
	__builtin_strcpy(vcsa+9,tty_name_buf+8);
	int f=open(tty_name_buf,O_RDWR|O_NOCTTY);
	if(f<0||ioctl(f,KDGETMODE,&gfxp)) __builtin_abort(); /* sysadmin doing something weird... */
	if(c>=0 && c!=f)close(c),c=f;
	loadfont();W.ws_col=W.ws_row=0;
	if((f=open(vcsa,O_RDWR|O_NONBLOCK|O_NOCTTY))<0||fasync(f,DEV_VCS_SIG))close(f);else (v>=0&&close(v)),dimx=dimn=0,v=f;
	active=0;
}
static void loadscreen(int _){
	static int lastactive=-1;
	struct vt_stat st;int r;
	struct winsize w;

	for(;;){
		if(!ioctl(c,VT_GETSTATE,&st)){
			if(lastactive!=st.v_active)openscreen(lastactive=st.v_active);
		} else __builtin_abort();
		if(vt_mask_active)return;
		if(!ioctl(c,TIOCGWINSZ,&w) && (w.ws_row!=W.ws_row||w.ws_col!=W.ws_col)) {
			if(screen)munmap(screen,4+(dimx+dimn)*2);W=w;dimx=W.ws_col;dimn=dimx*W.ws_row;mapvideo();
		} else if(dimn<1)__builtin_abort(); else if((4+2*dimn)==(r=read(v,screen,2*dimn+4)))return;
	}
}
static void setscreen(void){if(vt_mask_active)return; while((2*dimn)!=pwrite(v,screen+4,2*dimn,4)); }

static void drawcursor(int x, int y, int bm){
	if(vt_mask_active)return;

	if(gfxp)return;

	if(x<0||y<0||x>=pX(W.ws_col)||y>=pY(W.ws_row))return; /* nothing to draw */

	if(fontx) {
		erasecursor();
		C(stash, S(X(x),Y(y)), 2);
		shadow[0]=4; if(!(shadow[1]=stash[1]^255))shadow[1]=15; /* the cursor */
		C(S(X(x),Y(y)),shadow,2);
		setscreen();
	} else {
		if(bm) {
			erasecursor();
			C(stash, S(X(x),Y(y)), 4);
			C(stash+4, S(X(x),Y(y)+1), 4);
		}

		N(4,C(font+fp*P[i],font+fp*stash[i*2],fp));

		unsigned char *p;int k=sizeof(cursor)-(y%fh),j;

		p=font+fp*1; j=k; N(fh,*p++ |= cursor[j++%sizeof(cursor)]>>(x&7));
		p=font+fp*2;      N(fh,*p++ |= cursor[j++%sizeof(cursor)]>>(x&7));

		if(X(x)<(W.ws_col-1)) {
			p=font+fp*3; j=k; N(fh,*p++ |= cursor[j++%sizeof(cursor)]<<(8-(x&7)));
			p=font+fp*4;      N(fh,*p++ |= cursor[j++%sizeof(cursor)]<<(8-(x&7)));
		}

		N(4,shadow[i*2]=P[i]);N(4,shadow[(i*2)+1]=stash[(i*2)+1]);N(4,if(!shadow[1+i*2])shadow[1+i*2]=15);
		C(S(X(x),Y(y)),shadow,4);C(S(X(x),Y(y)+1),shadow+4,4);setfont();
		if(bm)setscreen();
	}
}
static void erasecursor(void) {
	if(vt_mask_active||gfxp)return;
	unsigned char c; // need to look for the poisoned character because of scrolling
	if(fontx){N(dimn,if((c=screen[4+i*2])==*shadow)screen[4+i*2]=*stash,screen[5+i*2]=stash[1]);}
	else N(dimn,if((c=screen[4+i*2])<5&&c)screen[4+i*2]=stash[(P[c-1]-1)*2],screen[5+i*2]=stash[(P[c-1]-1)*2+1]);
}

#define S(X) (struct iovec){.iov_base=(X),.iov_len=__builtin_strlen(X)} /* no need for screen after this */
static void dev_input(int _, siginfo_t*s,void*u){ // DEV_INPUT_SIG
	static int x=0,y=0;
	static struct __attribute__((__packed__)) {
		unsigned char subcode;
		struct tiocl_selection s;
	}k={0};
	static struct input_event ev[128];
	static int last_code = 0;
	static long long last_click[2]={0};
	static char btn = 0;
	static int need_paste = 0;

	int r,absxy=0,z=0;

	int fd=s->si_fd,ox=x,oy=y,oa=active;
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME,&ts);

retry:	while((r=read(fd,ev,sizeof(ev)))>0) {
		r /= sizeof(*ev);
		if(!active){active=1;k.s.sel_mode=TIOCL_SELCLEAR;ioctl(c,TIOCLINUX,&k);}
		for(int i = 0; i< r;++i){
			struct input_event*e=ev+i;
			switch(e->type){
			case EV_KEY:
				switch(e->code){
				case KEY_LEFTALT:
				case KEY_LEFTMETA:
					if(!e->value && last_code == e->code && stuffn) { /* hotkey! */
						if(vt_mask_active)break;
						for(int i=0;i<stuffn;++i)ioctl(c,TIOCSTI,stuff+i);
					}
					break;
				case BTN_LEFT:
					if(e->value) {
						if(btn) break;
						erasecursor();oa=0;setscreen();
						k.s.xe = k.s.xs=X(x)+1; k.s.ye=k.s.ys=Y(y)+1;
						k.subcode=TIOCL_SETSEL;k.s.sel_mode=TIOCL_SELCHAR;
						btn|=1;
					} else {
						btn&=~1;
						k.s.xe = X(x)+1; k.s.ye=Y(y)+1;
						if(k.s.xe == k.s.xs && k.s.ye == k.s.ys) {
							long long now=ts.tv_sec;
							if((now-last_click[0])<2){
								k.subcode=TIOCL_SETSEL;
								k.s.sel_mode=last_click[0]==last_click[1]?TIOCL_SELLINE:TIOCL_SELWORD;
								if(oa){erasecursor();setscreen();oa=0;}ioctl(c,TIOCLINUX,&k);
							} else {
								k.subcode=TIOCL_SELCLEAR;
							}
							last_click[1]=last_click[0];last_click[0]=now;
						}
					}
					break;
				case BTN_RIGHT:
				case BTN_MIDDLE:
					if(e->value)need_paste=1,btn=0;
					break;
				};
				last_code= e->code;
				break;
			case EV_ABS: // these seem to come in very high rate on some devices, so just do the last one in each dimension
				switch(e->code){
				case ABS_X: x = e->value; absxy|=2; break;
				case ABS_Y: y = e->value; absxy|=1; break;
				};
				break;
			case EV_REL:
				switch(e->code){
				case REL_WHEEL:z+=e->value<0?1:-1;break;
				case REL_X:x+=e->value*2;break;
				case REL_Y:y+=e->value*2;break;
				};
			};
		}
	}
	if(r==-1&&errno == EINTR)goto retry; 
	if(absxy){ // process the last absolute events
		struct input_absinfo aa;int A[]={ABS_Y,ABS_X},a[2]={y,x};char h[]={fh,8};
		unsigned short d[] = { W.ws_row, W.ws_col };
		for(int i=0;i<2;++i) {
			if(!(absxy&(1<<i)))continue; // no update on this axis?
			a[i] = ioctl(fd,EVIOCGABS(A[i]),&aa) ? 
				i?ox:oy // weird: we got an absolute event, but no dimensions? just put the cursor back
				// seems like we might get into this branch if there's a vt switch we haven't gotten notified yet...
			:((long long)a[i] + aa.minimum) * ((unsigned long long)(d[i]*h[i])) /  (aa.maximum-aa.minimum);
		}
		y=a[0];x=a[1];
	}
	if(x<0)x=0;else if(x>=pX(W.ws_col))x=pX(W.ws_col)-1;
	if(y<0)y=0;else if(y>=pY(W.ws_row))y=pY(W.ws_row)-1;
	if(btn) {
		if((X(ox)!=X(x)||Y(oy)!=Y(y))){
			k.subcode=TIOCL_SETSEL;k.s.sel_mode=TIOCL_SELCHAR;
			k.s.xe = X(x)+1; k.s.ye=Y(y)+1;
			ioctl(c,TIOCLINUX,&k);oa=0;
		}
	} else if(active){
		if(z) {
			struct __attribute__((__packed__)) {
				unsigned char subcode;
				int v;
			}k2={0};
			if(oa){erasecursor();setscreen();oa=0;}
			k2.subcode=TIOCL_SCROLLCONSOLE;k2.v=z;ioctl(c,TIOCLINUX,&k);
			loadscreen(-1);
		}
		if((X(ox)!=X(x)||Y(oy)!=Y(y))){
			struct __attribute__((__packed__)) {
				unsigned char subcode;
				struct tiocl_selection s;
			}k2={0};
			k2.subcode=TIOCL_SETSEL;
			k2.s.sel_mode=TIOCL_SELMOUSEREPORT+btn;
			k2.s.xe = k2.s.xs=X(x)+1; k2.s.ye=k2.s.ys=Y(y)+1;
			ioctl(c,TIOCLINUX,&k2);
		}
		if(!oa||(ox!=x||oy!=y)){
			drawcursor(x,y,!oa||X(x)!=X(ox)||Y(y)!=Y(oy));
		}
		if(need_paste) {
			k.subcode=TIOCL_PASTESEL;
			ioctl(c,TIOCLINUX,&k);
			need_paste = 0;
		}
	}
}
static void dev_tty(int _, siginfo_t*s,void*u){ // DEV_TTY_SIG
	char buf[128];
	int r;
	if((r=read(s->si_fd,buf,sizeof(buf)))>0) {
		for(int i=0;i<r;++i)ioctl(c,TIOCSTI,stuff+i);
	}
}

static int setup(int i,const char*dev) {
	struct stat sb;

	// allow overriding filesystem for checking devices; we've already validated access to some virtual console
	int f=open(dev,O_RDONLY|O_NOCTTY),e=f;

	if((e=f)<0)goto err;
	if((e=fstat(f,&sb))){close(f);goto err;}
	switch(sb.st_mode & S_IFMT){
	case S_IFCHR:
		switch (sb.st_rdev >> 8) {
		case 13: /* /dev/input/ attach keyboard or mouse */
			if((e=fasync(f,DEV_INPUT_SIG))) {
err:		struct iovec iov[4]={S((char*)dev),S(": "),S(strerror(errno)),S("\n")};
				W2(iov,4);
				if(f>=0)close(f);
				return 0;
			};
			return 1;
		case 1: /* /dev/mem? ... */
		case 5: goto notsup; /* /dev/console? /dev/ptmx? /dev/tty? idk what to do with this */

		case 7:vcs: /* /dev/ttyN and /dev/vcs*N ; limit access to specific screens */
			if(sb.st_rdev & 63)vt_mask |= 1ull<<((sb.st_rdev & 63)-1); vt_mask_set=1;
			close(f);return 0;

		case 4:  /* serial devices? forward them to the console it's a party! */
			if((sb.st_rdev&255)<64) goto vcs; /* but not other consoles; that's masking like vcs... */
			/* /dev/ttyS ... */
		case 188: /* ttyUSB */ case 224:case 208:case 204: /* --- ,,, and a mess of other serial ports */
case 172:case 174:case 164:case 161:case 156:case 154:case 148:case 105:case 75:case 71:case 57:case 48:case 46:case 32:case 24:case 22:
		case 136: /* ... and /dev/pts/N */
		case 3: /* ... and bsd /dev/ttypX can all stuff the console keyboard */
			if((e=fasync(f,DEV_TTY_SIG)))goto err;
			return 1;

		}; /* switch */
notsup:
		close(f);
		do {
			struct iovec iov[2]={S((char*)dev),S(": Not supported\n")};
			W2(iov,2);
		}while(0);
		return 0;
	case S_IFIFO:	if((e=fasync(f,DEV_TTY_SIG)))goto err;return 1;
	case S_IFREG:
		if(stuffn){close(f);break;} /* only one stuff-buffer */
		int r=pread(f,stuff,sizeof(stuff),0);
		close(f);
		if(r>255) {
			struct iovec iov[2]={S((char*)dev),S(": Hotkey stuffing limited to 255 bytes\n")};
			W2(iov,2);
			return 0;
		}
		stuffn=r;
		return 1;
	default:
		close(f);
	};
	do {
		struct iovec iov[2]={S((char*)dev),S(": Ignored\n")};
		W2(iov,2);
		return 0;
	}while(0);
	
}
static void mapvideo(void){
	if((screen = mmap(NULL,4+2*(dimx+dimn),PROT_READ|PROT_WRITE,MAP_POPULATE|MAP_PRIVATE|MAP_ANONYMOUS,-1,0))==MAP_FAILED) {
		struct iovec iov[3]={S("mmap: "),S(strerror(errno)), S("\n")};
		W2(iov,3);
		exit(1);
	}
}

int main(int argc, char *argv[])
{
	struct stat sb;

	if(argc==1){
		struct iovec iov[3]={S("usage: "),S(argv[0]),S(" /dev/input/by-id/*-mouse\n")};
		W2(iov,3);
		exit(1);
	}

	c=-1; // check first few fds; we're probably already attached to a console
	for(int r,i = 0; i < 3; ++i) {
		if(fstat(i,&sb) < 0) continue;
		if((sb.st_rdev>>8)!=4)continue;
		if((r=ioctl(i,KDGETMODE,&gfxp))<0) {
			ino(tty_name_buf+8,sb.st_rdev&255); // idk if this is actually helpful; what is happening here?
			struct iovec iov[3]={S(tty_name_buf), S(strerror(errno)), S("\n")};
			W2(iov,3);
			exit(1);
		}

		c=dup(i);break;
	}
	if(c<0) {
		// maybe i'm logged in on another console...
		for(int i = 1; i < 128; ++i) {
			ino(tty_name_buf+8,i);
			if((c=open(tty_name_buf,O_RDWR|O_NOCTTY))<0)continue;
			if(ioctl(c,KDGETMODE,&gfxp)<0)close(c),c=-1;else break;
		}
		// maybe i'm root
		if(c<0 && (c = open("/dev/console",O_RDWR|O_NOCTTY)) < 0 || ioctl(c,KDGETMODE,&gfxp)<0) {
			struct iovec iov[3]={S("/dev/console: "), S(strerror(errno)), S("\n")};
			W2(iov,3);
			exit(1);
		}
	}
	loadfont();

	v = -1, dimx=dimn = 0; /* loadscreen will handle */
	mapvideo(); dimn=-1; 

	signal(SIGTTIN,SIG_IGN);signal(SIGTTOU,SIG_IGN);signal(SIGTSTP,SIG_IGN);

	int a;
	struct sigaction sa={0};sa.sa_sigaction=dev_input;
	sa.sa_flags = SA_SIGINFO|SA_RESTART;
	if((a=sigaction(DEV_INPUT_SIG,&sa,NULL))<0) {
		struct iovec iov[3]={S("sigaction: "), S(strerror(errno)), S("\n")};
		W2(iov,3);
		exit(1);
	}
	sa.sa_sigaction=dev_tty;
	if((a=sigaction(DEV_TTY_SIG,&sa,NULL))<0) {
		struct iovec iov[3]={S("sigaction: "), S(strerror(errno)), S("\n")};
		W2(iov,3);
		exit(1);
	}
	sa.sa_handler=loadscreen;
	sa.sa_flags = SA_RESTART;
	if((a=sigaction(DEV_VCS_SIG,&sa,NULL))<0) {
		struct iovec iov[3]={S("sigaction: "), S(strerror(errno)), S("\n")};
		W2(iov,3);
		exit(1);
	}

	int any=0;
	loadscreen(0);
	N(argc-1,if(setup(i,argv[i+1]))any++);

	if(any) {
		do {
			struct iovec iov[]={S("ready: "),
				S(gfxp==KD_TEXT?" con":" kms"),
				S(fontx?"\n":"/vga\n")};
			W2(iov,sizeof(iov)/sizeof(*iov));
		}while(0);
// possible daemon/background?
		while(1)pause();
	}
	exit(100);
}
