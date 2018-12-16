#include "nvse/nvse/PluginAPI.h"
#include "common/IDebugLog.h"
#include "nvse/nvse/nvse_version.h"
#include "nvse/nvse/Hooks_DirectInput8Create.h"
#include "nvse/nvse/SafeWrite.h"
#include <string>
#include <windows.h>

//IDebugLog gLog("nvse_console_clipboard.log");

static const int backspaceChar = 0x80000000;
static const int rightArrowChar = 0x80000002;
static const int leftArrowChar = 0x80000001;
static const int deleteChar = 0x80000007;
static const int enterChar = 0x80000008;
static const int pageUpChar = 0x80000009;
static const int pageDownChar = 0x8000000A;

/* helper function prototypes */
void handleIniOptions();
void patchOnConsoleInput();
void GetClipboardText(char **buffer);
void __fastcall PrintToConsoleInput(UInt32 character);
char *getConsoleInputString();
bool versionCheck(const NVSEInterface* nvse);
int indexOfChar(char *text, char c);


bool CheckHotkeys();
/* called in ASM hook
* all have a dummy return true, as required for where is hooked
*/
bool __fastcall PrintClipBoardToConsoleInput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall DeletePreviousWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall DeleteNextWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall clearInputString(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall MoveToEndOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall MoveToStartOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall copyInputToClipboard(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall ClearConsoleOutput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);

NVSEInterface *SaveNVSE;
DIHookControl *g_DIHookCtrl = NULL;
HMODULE consolePasteHandle;

int g_bReplaceNewLineWithEnter = 0; // 0 -> replace with space
int g_iConsoleMaxLines = 15;
const int MAX_BUFFER_SIZE = 512;

static const UInt32 GetConsoleManager = 0x71B160;
static const UInt32 sendCharToInput = 0x71B210;

#define ClearLines(consoleManager) ThisStdCall(0x71E070, consoleManager+1);
#define RefreshConsoleOutput(consoleManager) ThisStdCall(0x71D410, consoleManager);

static char *clipboardText = (char*)malloc(MAX_BUFFER_SIZE);

extern "C" {

	BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
	{
		if (dwReason == DLL_PROCESS_ATTACH)
			consolePasteHandle = (HMODULE)hDllHandle;
		return TRUE;
	}

	bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info) {
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "Console Clipboard";
		info->version = 1;

		handleIniOptions();

		return versionCheck(nvse);
	}

	bool NVSEPlugin_Load(const NVSEInterface *nvse) {

		NVSEDataInterface *nvseData = (NVSEDataInterface *)nvse->QueryInterface(kInterface_Data);
		g_DIHookCtrl = (DIHookControl *)nvseData->GetSingleton(NVSEDataInterface::kNVSEData_DIHookControl);

		patchOnConsoleInput();
		return true;
	}

};

void handleIniOptions() {
	char filename[MAX_PATH];
	GetModuleFileNameA(consolePasteHandle, filename, MAX_PATH);
	strcpy((char *)(strrchr(filename, '\\') + 1), "nvse_console_clipboard.ini");
	g_bReplaceNewLineWithEnter = GetPrivateProfileIntA("Main", "bReplaceNewLineWithEnter", 0, filename);
}

void patchOnConsoleInput() {
	UInt32 onConsoleInputAddress = 0x70E09E;
	WriteRelCall(onConsoleInputAddress, (UInt32)CheckHotkeys);
}

/*
EAX will contain the character pressed
ECX contains the location of the ConsoleManager singleton
*/
__declspec(naked) bool CheckHotkeys()
{
	static const UInt32 retnAddr = 0x71B210;
	static const UInt32 getIsMenuModeCall = 0x7023A0;
	__asm
	{
		movsx	eax, [ecx + 0x38] //ConsoleManager::isConsoleOpen
		test	eax, eax
		jle		consoleMenuNotOpen
		push ecx
		mov     ecx, [0x11D8A80]
		call    getIsMenuModeCall //InterfaceManager::IsMenuMode()
		pop ecx
		movzx   eax, al
		test    eax, eax
		jz      consoleMenuNotOpen
		mov     eax, g_DIHookCtrl
		cmp     byte ptr[eax + 0xCF], 0    // check left control
		jnz     checkHotkeys
		cmp     byte ptr[eax + 0x44F], 0    // check right control
		jz      handleNormalInput
		checkHotkeys :
		mov     eax, [esp + 4]
			cmp     eax, 0x80000000
			jz      handleBack
			cmp     eax, 0x80000001
			jz      handleLeft
			cmp     eax, 0x80000002
			jz      handleRight
			cmp     eax, 0x80000007
			jz      handleDelete
			or al, 0x20    // Change to lower-case
			cmp     al, 'v'
			jz      handleV
			cmp		al, 'l'
			jz		handleL
			cmp     al, 'c'
			jz      handleC
			cmp     al, 'x'
			jnz     handleNormalInput
			jmp     clearInputString
			handleBack :
		jmp     DeletePreviousWord
			handleLeft :
		jmp     MoveToStartOfWord
			handleRight :
		jmp     MoveToEndOfWord
			handleDelete :
		jmp     DeleteNextWord
			handleV :
		jmp     PrintClipBoardToConsoleInput
			handleL :
		jmp ClearConsoleOutput
			handleC :
		jmp     copyInputToClipboard
			consoleMenuNotOpen :
		mov eax, [esp + 4] // restore original eax
			handleNormalInput :
			jmp     retnAddr
	}
}


bool __fastcall PrintClipBoardToConsoleInput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	GetClipboardText(&clipboardText);
	for (int i = 0, c = clipboardText[0]; c != '\0' && i < MAX_BUFFER_SIZE; i++) {
		c = clipboardText[i];
		switch (c) {
			/* replace newlines with spaces */
		case '\n':
		case '\r':
			if (g_bReplaceNewLineWithEnter) PrintToConsoleInput(enterChar);
			else PrintToConsoleInput(' ');
			break;

			/* remove pipe characters */
		case '|':
			break;

		default:
			PrintToConsoleInput(clipboardText[i]);
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

void __fastcall PrintToConsoleInput(UInt32 characterToPrint) {
	__asm
	{
	getConsoleInputStringLocation:
		push  00
			call  GetConsoleManager // sets eax to location of console structure
			push  eax
			sendCharToConsoleInput :
		mov   eax, characterToPrint
			pop   ecx  // ecx = location of console struct
			push  eax
			call sendCharToInput
	}
}

/* check 0x14 for | character then check each row starting at the bottom
* this is a hacky solution until I work out how to get the address properly
*  - it fails if the console output has a line with one | in it.
*/
char *getConsoleInputString() {
	static const int CONSOLE_TEXT_BASE_ADDR = 0x11F33A8;
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

	/*
	__asm
	{
	mov eax, dword ptr ds : [0x11F33A8]
	mov eax, dword ptr ds : [eax+0x14]
	}*/
}

int indexOfChar(char *text, char c) {
	int index = 0;
	for (; text[index] != '\0'; index++)
		if (text[index] == c)
			return index;

	return -1;
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
		PrintToConsoleInput(backspaceChar);
	}
	return true;
}

bool __fastcall DeleteNextWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !(*consoleInput)) return true;

	int charsToDelete = getCharsTillSpace(consoleInput);
	for (; charsToDelete > 0; charsToDelete--) {
		PrintToConsoleInput(deleteChar);
	}
	return true;
}


bool __fastcall MoveToStartOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !(*consoleInput)) return true;

	int charsToMove = getCharsSinceSpace(consoleInput);
	for (; charsToMove > 0; charsToMove--) {
		PrintToConsoleInput(leftArrowChar);
	}
	return true;
}

bool __fastcall MoveToEndOfWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !(*consoleInput)) return true;

	int charsToMove = getCharsTillSpace(consoleInput);
	for (; charsToMove > 0; charsToMove--) {
		PrintToConsoleInput(rightArrowChar);
	}
	return true;
}

bool __fastcall clearInputString(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *buffer = getConsoleInputString();
	buffer[0] = '\0';
	PrintToConsoleInput(rightArrowChar); //any control character would work here

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

bool __fastcall copyInputToClipboard(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
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

bool versionCheck(const NVSEInterface* nvse) {
	if (nvse->isEditor) return false;
	if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525) {
		_ERROR("incorrect runtime version (got %08X need at least %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
		return false;
	}
	return true;
}

bool __fastcall ClearConsoleOutput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	ClearLines(consoleMgr); // clears the TextList printedLines
	*(consoleMgr + 9) = 0;
	RefreshConsoleOutput(consoleMgr); // refreshes the output so changes show
	return true;
}
