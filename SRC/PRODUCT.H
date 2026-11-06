/* Product settings shared by the native NT 3.1 installer UI and engine. */
#ifdef FP32_SETUP
#define PRODUCT "Flash Player 32"
#define DEFAULT_INSTALL_ROOT "C:\\FP32"
#define PLAYER_EXE "FP32.EXE"
#define PLAYER_ICON "FP32.EXE"
#define PLAYER_CHILD "APP\\FP32.EXE"
#define MODERN_SUBDIR "Flash Player 32"
#define CLASS "Ages2001.FP32.SWF"
#define PACKAGE_MAX_FILES 32
#define PRODUCT_REQUIREMENTS "\r\n\r\nRequires an SSE2-capable CPU. On NT 3.1/3.5x/4.0 and Windows 95/98/ME, install an SSE2 enabler application separately (WINSIMD recommended); this setup does not install it. 2000/XP and later need no driver."
#else
#define PRODUCT "Flash Player 10"
#define DEFAULT_INSTALL_ROOT "C:\\FP10"
#define PLAYER_EXE "FP10.EXE"
#define PLAYER_ICON "FP10.EXE"
#define PLAYER_CHILD ""
#define MODERN_SUBDIR "Flash Player 10"
#define CLASS "Ages2001.FP10.SWF"
#define PACKAGE_MAX_FILES 32
#define PRODUCT_REQUIREMENTS ""
#endif
#define GROUP PRODUCT
