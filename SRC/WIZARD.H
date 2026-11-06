/* Native NT 3.1 setup wizard; no common-controls dependency. */
enum { WIZ_BACK=1001,WIZ_NEXT=1002,WIZ_PATH=1003 };
static HWND wizard_window,wizard_edit,wizard_back,wizard_next,wizard_cancel;
static HBRUSH wizard_brush;static int wizard_page,wizard_done,wizard_result,wizard_success;
static char wizard_error[256],wizard_message[512];
static void wizard_show(void){
 ShowWindow(wizard_edit,wizard_page==1?SW_SHOW:SW_HIDE);
 ShowWindow(wizard_back,wizard_page<3?SW_SHOW:SW_HIDE);
 ShowWindow(wizard_cancel,wizard_page<3?SW_SHOW:SW_HIDE);
 EnableWindow(wizard_back,wizard_page>0);
 SetWindowTextA(wizard_next,wizard_page==2?"&Install":wizard_page==3?"&Finish":"&Next >");
 InvalidateRect(wizard_window,0,TRUE);UpdateWindow(wizard_window);
 SetFocus(wizard_page==1?wizard_edit:wizard_next);
}
static int wizard_path(void){char full[MAX_PATH],parent[MAX_PATH];DWORD n,i,attr;char*part;
 GetWindowTextA(wizard_edit,root,sizeof(root));n=len(root);
 /* NT 3.1 Program Manager drops custom icons when the icon source folder
    contains spaces, even though ExtractIcon and file opening accept it. */
 if(os_major==3)for(i=0;i<n;i++)if(root[i]==' '){copy(wizard_error,"On Windows NT 3.x / Windows 3.1x, choose a folder without spaces so Program Manager can display the shortcut icons.");return 0;}
 if(n<4||n>MAX_PATH-20||!((root[0]>='A'&&root[0]<='Z')||(root[0]>='a'&&root[0]<='z'))||root[1]!=':'||root[2]!='\\'){copy(wizard_error,"Enter a full local folder path, for example C:\\FP10.");return 0;}
 for(i=3;i<n;i++)if((BYTE)root[i]<32||root[i]=='"'||root[i]=='<'||root[i]=='>'||root[i]=='|'||root[i]=='?'||root[i]=='*'||root[i]==':'){copy(wizard_error,"The folder path contains an invalid character.");return 0;}
 while(n>3&&(root[n-1]=='\\'||root[n-1]=='/'))root[--n]=0;
 if(n<=3){copy(wizard_error,"Choose an application folder, not the drive root.");return 0;}
 n=GetFullPathNameA(root,sizeof(full),full,&part);if(!n||n>=MAX_PATH-20||!part||!*part){copy(wizard_error,"Windows could not resolve this folder path.");return 0;}
 copy(parent,full);i=(DWORD)(part-full);if(i>3)i--;parent[i]=0;attr=GetFileAttributesA(parent);
 if(attr==INVALID_FILE_ATTRIBUTES||!(attr&FILE_ATTRIBUTE_DIRECTORY)){copy(wizard_error,"The parent folder must already exist. Choose a folder inside an existing directory.");return 0;}
 attr=GetFileAttributesA(full);if(attr!=INVALID_FILE_ATTRIBUTES&&!(attr&FILE_ATTRIBUTE_DIRECTORY)){copy(wizard_error,"A file already uses this destination name.");return 0;}
 copy(root,full);SetWindowTextA(wizard_edit,root);wizard_error[0]=0;return 1;
}
static LRESULT CALLBACK wizard_proc(HWND w,UINT m,WPARAM a,LPARAM b){
 if(m==DM_GETDEFID)return MAKELONG(WIZ_NEXT,DC_HASDEFID);
 if(m==WM_CLOSE){wizard_done=1;wizard_result=0;return 0;}
 if(m==WM_ERASEBKGND){RECT r;GetClientRect(w,&r);FillRect((HDC)a,&r,wizard_brush);return 1;}
 if(m==WM_COMMAND){UINT id=LOWORD(a);if(id==IDCANCEL){wizard_done=1;wizard_result=0;return 0;}if(id==WIZ_BACK&&wizard_page>0&&wizard_page<3){wizard_page--;wizard_error[0]=0;wizard_show();return 0;}
  if(id==WIZ_NEXT){if(wizard_page==1&&!wizard_path()){InvalidateRect(w,0,TRUE);return 0;}if(wizard_page==2||wizard_page==3){wizard_done=1;wizard_result=1;return 0;}wizard_page++;wizard_show();return 0;}}
 if(m==WM_PAINT){PAINTSTRUCT ps;HDC dc=BeginPaint(w,&ps);RECT r;const char*title;GetClientRect(w,&r);FillRect(dc,&r,wizard_brush);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,GetSysColor(COLOR_BTNTEXT));r.left=20;r.right=490;r.top=20;r.bottom=50;
  title=wizard_page==0?"Welcome to " PRODUCT " Setup":wizard_page==1?"Choose the destination folder":wizard_page==2?"Ready to install":wizard_success?"Installation complete":"Installation failed";
  DrawTextA(dc,title,-1,&r,DT_LEFT|DT_WORDBREAK);r.top=65;r.bottom=315;
  if(wizard_page==0)DrawTextA(dc,"Setup will install " PRODUCT ", create two shortcuts and associate SWF files.\r\n\r\nPorted to Windows NT 3.1/3.5x/4/2000/XP and Windows 95/98/ME by ages2001." PRODUCT_REQUIREMENTS,-1,&r,DT_LEFT|DT_WORDBREAK);
  else if(wizard_page==1){r.bottom=92;DrawTextA(dc,"Install " PRODUCT " in this folder:",-1,&r,DT_LEFT);r.top=150;r.bottom=315;SetTextColor(dc,RGB(128,0,0));DrawTextA(dc,wizard_error,-1,&r,DT_LEFT|DT_WORDBREAK);}
  else if(wizard_page==2){char review[512];copy(review,"Destination:\r\n");cat(review,root);cat(review,"\r\n\r\nSetup will create two shortcuts and register SWF files. The previous association will be saved for uninstall.\r\n\r\nClick Install to begin.");DrawTextA(dc,review,-1,&r,DT_LEFT|DT_WORDBREAK);}
  else DrawTextA(dc,wizard_message,-1,&r,DT_LEFT|DT_WORDBREAK);
  EndPaint(w,&ps);return 0;}
 return DefWindowProcA(w,m,a,b);
}
static void wizard_pump(void){MSG m;while(!wizard_done&&GetMessageA(&m,0,0,0)>0){UINT cmd=0;
 if(m.message==WM_KEYDOWN&&m.wParam=='A'&&(GetKeyState(VK_CONTROL)&0x8000)&&GetFocus()==wizard_edit){SendMessageA(wizard_edit,EM_SETSEL,0,-1);continue;}
 /* NT 3.1 does not apply dialog default-button/mnemonic handling reliably
    to a custom top-level window. Handle these keys explicitly. */
 if(m.message==WM_KEYDOWN&&m.wParam==VK_ESCAPE)cmd=IDCANCEL;
 if(m.message==WM_KEYDOWN&&m.wParam==VK_RETURN){HWND focused=GetFocus();cmd=focused==wizard_back?WIZ_BACK:focused==wizard_cancel?IDCANCEL:WIZ_NEXT;}
 if(m.message==WM_SYSCHAR){UINT key=(UINT)m.wParam;if(key>='A'&&key<='Z')key+='a'-'A';if(key=='n'||key=='i'||key=='f')cmd=WIZ_NEXT;else if(key=='b')cmd=WIZ_BACK;else if(key=='c')cmd=IDCANCEL;}
 if(cmd){SendMessageA(wizard_window,WM_COMMAND,cmd,0);continue;}
 if(!IsDialogMessageA(wizard_window,&m)){TranslateMessage(&m);DispatchMessageA(&m);}}}
static void wizard_end(void){if(wizard_window)DestroyWindow(wizard_window);wizard_window=0;if(wizard_brush)DeleteObject(wizard_brush);wizard_brush=0;}
static int wizard_begin(void){WNDCLASSA c;DWORD i;HINSTANCE inst=GetModuleHandleA(0);for(i=0;i<sizeof(c);i++)((BYTE*)&c)[i]=0;
 wizard_brush=CreateSolidBrush(GetSysColor(COLOR_BTNFACE));c.hInstance=inst;c.lpfnWndProc=wizard_proc;c.hCursor=LoadCursorA(0,IDC_ARROW);c.hIcon=LoadIconA(inst,MAKEINTRESOURCEA(101));c.hbrBackground=wizard_brush;c.lpszClassName="FP10SetupWizard";
 if(!RegisterClassA(&c)){wizard_end();return -1;}
 wizard_window=CreateWindowExA(WS_EX_DLGMODALFRAME,c.lpszClassName,PRODUCT " Setup",WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,(GetSystemMetrics(SM_CXSCREEN)-520)/2,(GetSystemMetrics(SM_CYSCREEN)-400)/2,520,400,0,0,inst,0);if(!wizard_window){wizard_end();return -1;}
 wizard_edit=CreateWindowExA(0,"EDIT",root,WS_CHILD|WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,20,100,470,25,wizard_window,(HMENU)WIZ_PATH,inst,0);SendMessageA(wizard_edit,EM_LIMITTEXT,MAX_PATH-21,0);
 wizard_back=CreateWindowA("BUTTON","< &Back",WS_CHILD|WS_VISIBLE|WS_TABSTOP,200,330,90,26,wizard_window,(HMENU)WIZ_BACK,inst,0);
 wizard_next=CreateWindowA("BUTTON","&Next >",WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,300,330,90,26,wizard_window,(HMENU)WIZ_NEXT,inst,0);
 wizard_cancel=CreateWindowA("BUTTON","Cancel",WS_CHILD|WS_VISIBLE|WS_TABSTOP,400,330,90,26,wizard_window,(HMENU)IDCANCEL,inst,0);
 if(!wizard_edit||!wizard_back||!wizard_next||!wizard_cancel){wizard_end();return -1;}
 ShowWindow(wizard_window,SW_SHOW);wizard_show();wizard_pump();ShowWindow(wizard_window,SW_HIDE);if(!wizard_result)wizard_end();return wizard_result;
}
static void wizard_complete(int ok,const char*message){wizard_success=ok;copy(wizard_message,message);wizard_page=3;wizard_done=0;ShowWindow(wizard_window,SW_SHOW);wizard_show();wizard_pump();wizard_end();}
