#include "common/ITypes.h"
#include <Windows.h>
#include <string>
#include "obse\PluginAPI.h"
#include "obse\GameAPI.h"
#include "obse_common\SafeWrite.h"
#include "obse/Hooks_DirectInput8Create.h"
//#include "stdcalls.h"


enum
{
  kSpclChar_Backspace = 0x80000000,
  kSpclChar_LeftArrow = 0x80000001,
  kSpclChar_RightArrow = 0x80000002,
  kSpclChar_Delete = 0x80000007,
  kSpclChar_Enter = 0x80000008,
};

void CheckCTRLHotkeys();
bool versionCheck(const OBSEInterface* obse);
void GetClipboardText(char **buffer);

const UInt32 ConsoleManager_GetSingleton = 0x585C10;
const UInt32 kConsoleSendInput = 0x586000;
const UInt32 hookLocation = 0x583550;

const int MAX_BUFFER_SIZE = 512;
static char *clipboardText = (char*)malloc(MAX_BUFFER_SIZE);

extern "C" {

  BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved) {
    return TRUE;
  }

bool OBSEPlugin_Query(const OBSEInterface * obse, PluginInfo * info) {
    /* fill out the info structure */
    info->infoVersion = PluginInfo::kInfoVersion;
    info->name = "Console Clipboard";
    info->version = 1;

    return versionCheck(obse);
  }

bool OBSEPlugin_Load(const OBSEInterface * obse) {
    WriteRelCall(hookLocation, UInt32(CheckCTRLHotkeys));
    return true;
  }

};

bool __fastcall PrintClipBoardToConsoleInput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  GetClipboardText(&clipboardText);
  for (int i = 0, c = clipboardText[0]; c != '\0' && i < MAX_BUFFER_SIZE; i++) {
    c = clipboardText[i];
    switch (c) {
    
    /* replace newlines with spaces */
    case '\n':
      ThisStdCall(kConsoleSendInput, consoleMgr, ' ');
      break;

    /* remove pipe and \r characters */
    case '\r':
    case '|':
      break;

    default:
      ThisStdCall(kConsoleSendInput, consoleMgr, clipboardText[i]);
    }
  }
  return true;
}


void GetClipboardText(char **buffer) {
  /* Try opening the clipboard */
  if (!OpenClipboard(NULL)) {
    // clipboard empty so return empty string
    *buffer[0] = '\0';
    return;
  }

  /* Get handle of clipboard object for ANSI text */
  HANDLE hData = GetClipboardData(CF_TEXT);
  if (hData == NULL) {
    CloseClipboard();
    // clipboard empty so return empty string
    *buffer[0] = '\0';
    return;
  }

  /* Lock the handle to get the actual text pointer */
  char *pszText = static_cast<char *>(GlobalLock(hData));
  if (pszText == NULL) {
    GlobalUnlock(hData);
    CloseClipboard();
    // clipboard empty so return empty string
    *buffer[0] = '\0';
    return;
  }

  /* Save text in a string class instance */
  std::string text(pszText);

  /* Release the lock and clipboard */
  GlobalUnlock(hData);
  CloseClipboard();

  /* copy clipboard text into buffer */
  strncpy(*buffer, text.c_str(), MAX_BUFFER_SIZE);
}

bool isCtrlHeld() {
  SHORT ctrlKeyState = GetAsyncKeyState(VK_CONTROL);
  // Test high bit - if set, key was down when GetAsyncKeyState was called.
  return ((1 << 15) & ctrlKeyState);
}


int indexOfChar(char *text, char c) {
  int index = 0;
  for (; text[index] != '\0'; index++)
    if (text[index] == c)
      return index;

  return -1;
}


/* check 0x14 for | character then check each row starting at the bottom
* this is a hacky solution until I work out how to get the address properly
*  - it fails if the console output has a line with one | in it.
*/
char *getConsoleInputString() {
  static const int CONSOLE_TEXT_BASE_ADDR = 0xB3A6A4;
  static const int LINE_STRUCT_SIZE = 0x2C;
  static const int firstTextOffset = 0x14;
  static const int lastOffset = 0x384;

  int consoleLineAddress = *((int*)CONSOLE_TEXT_BASE_ADDR) + firstTextOffset;
  if (!consoleLineAddress) return NULL;

  char* consoleLine = *(char**)consoleLineAddress;
  if (consoleLine && indexOfChar(consoleLine, '|') > -1) return consoleLine;


  for (int i = lastOffset; i > firstTextOffset; i -= LINE_STRUCT_SIZE) {
    consoleLineAddress = *((int*)CONSOLE_TEXT_BASE_ADDR) + i;
    if (consoleLineAddress != NULL) {
      consoleLine = *((char**)consoleLineAddress);
      if (consoleLine && indexOfChar(consoleLine, '|') > -1) return consoleLine;
    }
  }
  return NULL;
}


int getCharsSinceSpace(char *text) {
  char *barPos = strchr(text, '|');
  int numChars = 0;

  while (barPos != text && !isalnum(*--barPos)) numChars++;
  while (barPos >= text && isalnum(*barPos--)) numChars++;

  return numChars;
}

int getCharsTillSpace(char *text) {
  char *barPos = strchr(text, '|');
  if (!barPos) return 0;

  int numChars = 0;

  while (isalnum(*++barPos)) numChars++;
  while (!isalnum(*barPos++) && *barPos) numChars++;

  return numChars;
}

bool __fastcall DeletePreviousWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *consoleInput = getConsoleInputString();
  if (!consoleInput || !(*consoleInput)) return true;

  int charsToDelete = getCharsSinceSpace(consoleInput);
  for (; charsToDelete > 0; charsToDelete--) {
    ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_Backspace);
  }
  return true;
}

bool __fastcall DeleteNextWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *consoleInput = getConsoleInputString();
  if (!consoleInput || !(*consoleInput)) return true;

  int charsToDelete = getCharsTillSpace(consoleInput);
  for (; charsToDelete > 0; charsToDelete--) {
    ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_Delete);
  }
  return true;
}


bool __fastcall MoveToStartOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *consoleInput = getConsoleInputString();
  if (!consoleInput || !(*consoleInput)) return true;

  int charsToMove = getCharsSinceSpace(consoleInput);
  for (; charsToMove > 0; charsToMove--) {
    ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_LeftArrow);
  }
  return true;
}

bool __fastcall MoveToEndOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *consoleInput = getConsoleInputString();
  if (!consoleInput || !(*consoleInput)) return true;

  int charsToMove = getCharsTillSpace(consoleInput);
  for (; charsToMove > 0; charsToMove--) {
    ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_RightArrow);
  }
  return true;
}

bool __fastcall ClearInputString(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *buffer = getConsoleInputString();
  buffer[0] = '\0';
  ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_RightArrow);
  return true;
}

void removeChar(char *str, const char garbage) {
  char *src, *dst;
  for (src = dst = str; *src != '\0'; src++) {
    *dst = *src;
    if (*dst != garbage) dst++;
  }
  *dst = '\0';
}

bool __fastcall CopyInputToClipboard(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
  char *inputString = getConsoleInputString();
  char* sanitisedInput = (char*)malloc(strlen(inputString) + 1);
  strcpy(sanitisedInput, inputString);
  removeChar(sanitisedInput, '|');

  OpenClipboard(NULL);
  EmptyClipboard();
  HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, strlen(sanitisedInput) + 1);
  if (!hg) {
    CloseClipboard();
    return true;
  }
  memcpy(GlobalLock(hg), sanitisedInput, strlen(inputString) + 1);
  GlobalUnlock(hg);
  SetClipboardData(CF_TEXT, hg);
  CloseClipboard();
  GlobalFree(hg);
  return true;
}


__declspec(naked) void CheckCTRLHotkeys()
{
  __asm
  {
    movsx  ebx, [ecx + 0x31] //ConsoleManager::isConsoleOpen
    test  ebx, ebx
    jle    consoleMenuNotOpen

    push ecx
    call isCtrlHeld
    pop ecx
    jz    handleNormalInput
  checkHotkeys :
    mov    ebx, [esp + 4]
    cmp    ebx, kSpclChar_Backspace
    jz    handleBack
    cmp    ebx, kSpclChar_LeftArrow
    jz    handleLeft
    cmp    ebx, kSpclChar_RightArrow
    jz    handleRight
    cmp    ebx, kSpclChar_Delete
    jz    handleDelete
    or bl, 0x20  // Change to lower-case
    cmp    ebx, 'v'
    jz    handleV
    cmp    ebx, 'c'
    jz    handleC
    cmp    ebx, 'x'
    jz    handleX
    mov    al, 1
    retn  4
      
  handleBack:
    jmp    DeletePreviousWord
  handleLeft :
    jmp    MoveToStartOfWord
  handleRight :
    jmp    MoveToEndOfWord
  handleDelete :
    jmp    DeleteNextWord
    
  handleV :
    jmp    PrintClipBoardToConsoleInput
  handleC :
    jmp    CopyInputToClipboard
  handleX :
    jmp    ClearInputString
    
  consoleMenuNotOpen:
    mov    ebx, [esp + 4]
  handleNormalInput :
    jmp    kConsoleSendInput
  }
}

bool versionCheck(const OBSEInterface* obse) {
 	if(obse->isEditor) return false;
	if(obse->obseVersion < OBSE_VERSION_INTEGER) {
		return false;
	}
	if(obse->oblivionVersion != OBLIVION_VERSION) {
		return false;
	}
	return true;
}
