#define WIN32_LEAN_AND_MEAN
#include <windows.h>
/* user32 gap closures: menu-item-info mapped onto the classic menu APIs, and
   SendInput mapped onto mouse_event/keybd_event (both on NT 3.1). No CRT. */

BOOL WINAPI fp_InsertMenuItemW(HMENU menu,UINT item,BOOL byPos,LPCMENUITEMINFOW mii){
    UINT flags, idNew; LPCWSTR str=0;
    if(!mii) return FALSE;
    flags = byPos?MF_BYPOSITION:MF_BYCOMMAND;
    if(mii->fMask & MIIM_SUBMENU && mii->hSubMenu){ flags|=MF_POPUP; idNew=(UINT)(UINT_PTR)mii->hSubMenu; }
    else idNew = (mii->fMask&MIIM_ID)?mii->wID:0;
    if(mii->fMask & (MIIM_STRING|MIIM_TYPE) && !(mii->fType&(MFT_SEPARATOR|MFT_BITMAP))){
        flags|=MF_STRING; str=mii->dwTypeData;
    }
    if(mii->fType & MFT_SEPARATOR) flags|=MF_SEPARATOR;
    if(mii->fMask & MIIM_STATE){
        if(mii->fState&MFS_CHECKED) flags|=MF_CHECKED;
        if(mii->fState&(MFS_DISABLED|MFS_GRAYED)) flags|=MF_GRAYED;
    }
    return InsertMenuW(menu,item,flags,idNew,str?str:(LPCWSTR)L"");
}
BOOL WINAPI fp_SetMenuItemInfoW(HMENU menu,UINT item,BOOL byPos,LPCMENUITEMINFOW mii){
    UINT by = byPos?MF_BYPOSITION:MF_BYCOMMAND;
    if(!mii) return FALSE;
    if(mii->fMask & (MIIM_STRING|MIIM_TYPE) && mii->dwTypeData)
        ModifyMenuW(menu,item,by|MF_STRING,(mii->fMask&MIIM_ID)?mii->wID:item,mii->dwTypeData);
    if(mii->fMask & MIIM_STATE){
        EnableMenuItem(menu,item,by|((mii->fState&(MFS_DISABLED|MFS_GRAYED))?MF_GRAYED:MF_ENABLED));
        CheckMenuItem(menu,item,by|((mii->fState&MFS_CHECKED)?MF_CHECKED:MF_UNCHECKED));
    }
    return TRUE;
}
BOOL WINAPI fp_GetMenuItemInfoW(HMENU menu,UINT item,BOOL byPos,LPMENUITEMINFOW mii){
    UINT by = byPos?MF_BYPOSITION:MF_BYCOMMAND;
    UINT state;
    if(!mii) return FALSE;
    state = GetMenuState(menu,item,by);
    if(state==(UINT)-1) return FALSE;
    if(mii->fMask & MIIM_STATE){
        mii->fState=0;
        if(state&MF_CHECKED) mii->fState|=MFS_CHECKED;
        if(state&MF_GRAYED)  mii->fState|=MFS_GRAYED;
        if(state&MF_DISABLED)mii->fState|=MFS_DISABLED;
    }
    if(mii->fMask & MIIM_ID) mii->wID = GetMenuItemID(menu,(int)item);
    if(mii->fMask & MIIM_SUBMENU) mii->hSubMenu = GetSubMenu(menu,(int)item);
    if(mii->fMask & MIIM_STRING && mii->dwTypeData && mii->cch>0)
        mii->cch = GetMenuStringW(menu,item,mii->dwTypeData,mii->cch,by);
    if(mii->fMask & (MIIM_TYPE|MIIM_FTYPE)) mii->fType = (state&MF_SEPARATOR)?MFT_SEPARATOR:MFT_STRING;
    return TRUE;
}
/* Menu-wide info (background, style) has no classic equivalent; accept it. */
BOOL WINAPI fp_SetMenuInfo(HMENU menu,LPCMENUINFO mi){(void)menu;(void)mi;return TRUE;}

UINT WINAPI fp_SendInput(UINT n,LPINPUT inputs,int cb){
    UINT i; (void)cb;
    if(!inputs) return 0;
    for(i=0;i<n;i++){
        INPUT*in=&inputs[i];
        if(in->type==INPUT_MOUSE){
            mouse_event(in->mi.dwFlags,in->mi.dx,in->mi.dy,in->mi.mouseData,in->mi.dwExtraInfo);
        } else if(in->type==INPUT_KEYBOARD){
            keybd_event((BYTE)in->ki.wVk,(BYTE)in->ki.wScan,in->ki.dwFlags,in->ki.dwExtraInfo);
        }
    }
    return n;
}
