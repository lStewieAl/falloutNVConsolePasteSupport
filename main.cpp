#include "nvse/nvse/PluginAPI.h"
#include "common/IDebugLog.h"
#include "nvse/nvse/nvse_version.h"
#include "nvse/nvse/Hooks_DirectInput8Create.h"
#include "nvse/nvse/SafeWrite.h"
#include <string>
#include <windows.h>

//IDebugLog gLog("nvse_console_clipboard.log");
HMODULE consolePasteHandle;

/* 0 means replace with space */
int g_bReplaceNewLineWithEnter = 0;

NVSEInterface *SaveNVSE;
DIHookControl *g_DIHookCtrl = NULL;

// function prototypes
void handleIniOptions();
void patchOnConsoleInput();
void __fastcall CheckCTRLV();
void GetClipboardText(char **buffer);
void __fastcall PrintToConsoleInput(UInt32 character);
char *getConsoleInputString();
bool versionCheck(const NVSEInterface* nvse);

int indexOfChar(char *text, char c);

static const UInt32 GetConsoleManager = 0x71B160;
static const UInt32 sendCharToInput = 0x71B210;
static char *clipboardText = (char *)malloc(512);


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
	WriteRelJump(onConsoleInputAddress, (UInt32)CheckCTRLV);
}


void PrintClipBoardToConsoleInput() {
	static const int enterCharacter = 0x80000008;
	GetClipboardText(&clipboardText);
	for (int i = 0, c = clipboardText[0]; c != '\0' && i < 511 /* limit size to console max script size */; i++) {
		c = clipboardText[i];
		switch (c) {
			/* replace newlines with spaces */
		case '\n':
		case '\r':
			if(g_bReplaceNewLineWithEnter) PrintToConsoleInput(enterCharacter);
			else PrintToConsoleInput(' ');
			break;

			/* remove pipe characters */
		case '|':
			break;

		default:
			PrintToConsoleInput(clipboardText[i]);
		}
	}
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
	strncpy(*buffer, text.c_str(), 511);
}

void __fastcall PrintToConsoleInput(UInt32 characterToPrint) {
	__asm
	{
	getConsoleInputStringLocation:
		push  00
		call  GetConsoleManager // sets eax to location of console structure
		push  eax
	sendCharToConsoleInput:
		mov   eax, characterToPrint
		pop   ecx  // eax = location of console struct
		push  eax
		call sendCharToInput
	}
}

/* check 0x14 for | character then check each row starting at the bottom
 * this is a hacky solution until I work out how to get the address properly
 *  - it fails if the console output has a line with one | in it.
 */
char *getConsoleInputString() {
	int consoleLineAddress = *((int*) 0x11F33A8) + 0x14;
	char* consoleLine = NULL;

	if (consoleLineAddress == NULL) return NULL;
	consoleLine = *(char**) consoleLineAddress;

	if (indexOfChar(consoleLine, '|') > -1) return consoleLine;


	for (int i = 0x2A8; i > 0x14; i -= 44) {
		consoleLineAddress = *((int*)0x11F33A8) + i;
		if(consoleLineAddress != NULL) {
			consoleLine = *((char**) consoleLineAddress);
			if (indexOfChar(consoleLine, '|') > -1) return consoleLine;
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

int getCharsSinceSpace(char *text, int caretIndex) {
	if (caretIndex == 0) return 0;
	char *caret = text + caretIndex;

	int charsSinceSpace = 0;

	while (!isalnum(*--caret)) charsSinceSpace++;
	while (isalnum(*--caret) && caret >= text) charsSinceSpace++;

	return charsSinceSpace + 1;
}

int getCharsTillSpace(char *text, int caretIndex) {
	char *caret = text + caretIndex;
	if (*++caret == '\0') return 0;

	int charsTillSpace = 0;

	while (isalnum(*caret++)) charsTillSpace++;
	while (*caret != '\0' && !isalnum(*caret++)) charsTillSpace++;

	return charsTillSpace + 1;
}

void DeletePreviousWord() {
	static const int backspaceChar = 0x80000000;
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !strlen(consoleInput)) return;

	int caretIndex = indexOfChar(consoleInput, '|');
	int charsToDelete = getCharsSinceSpace(consoleInput, caretIndex);
	for (; charsToDelete > 0; charsToDelete--) {
		PrintToConsoleInput(backspaceChar);
	}
}

void DeleteNextWord() {
	static const int deleteChar = 0x80000007;
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !strlen(consoleInput)) return;

	int caretIndex = indexOfChar(consoleInput, '|');
	int charsToDelete = getCharsTillSpace(consoleInput, caretIndex);
	for (; charsToDelete > 0; charsToDelete--) {
		PrintToConsoleInput(deleteChar);
	}
}


void MoveToStartOfWord() {
	static const int moveLeftChar = 0x80000001;
	char *consoleInput = getConsoleInputString();

	if (!consoleInput || !strlen(consoleInput)) return;

	int caretIndex = indexOfChar(consoleInput, '|');

	int charsToMove = getCharsSinceSpace(consoleInput, caretIndex);
	for (; charsToMove > 0; charsToMove--) {
		PrintToConsoleInput(moveLeftChar);
	}
}

void MoveToEndOfWord() {
	static const int moveRightCharacter = 0x80000002;
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !strlen(consoleInput)) return;

	int caretIndex = indexOfChar(consoleInput, '|');
	int charsToMove = getCharsTillSpace(consoleInput, caretIndex);
	for (; charsToMove > 0; charsToMove--) {
		PrintToConsoleInput(moveRightCharacter);
	}
}

void clearInputString() {
	static const int moveRightCharacter = 0x80000002;
	char *buffer = getConsoleInputString();
	buffer[0] = '\0';
	PrintToConsoleInput(moveRightCharacter); //any control character would work here
}

void removeChar(char *str, const char garbage) {
	char *src, *dst;
	for (src = dst = str; *src != '\0'; src++) {
		*dst = *src;
		if (*dst != garbage) dst++;
	}
	*dst = '\0';
}

void copyInputToClipboard() {
	char *inputString = getConsoleInputString();
	char* sanitisedInput = (char*)malloc(strlen(inputString) + 1);
	strcpy(sanitisedInput, inputString);
	removeChar(sanitisedInput, '|');

	OpenClipboard(NULL);
	EmptyClipboard();
	HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, strlen(sanitisedInput) + 1);
	if (!hg) {
		CloseClipboard();
		return;
	}
	memcpy(GlobalLock(hg), sanitisedInput, strlen(inputString) + 1);
	GlobalUnlock(hg);
	SetClipboardData(CF_TEXT, hg);
	CloseClipboard();
	GlobalFree(hg);
}

/*
EAX will contain the character pressed, v's code is 0x76
ECX contains the location of the input buffer to write to
*/
__declspec(naked) void __fastcall CheckCTRLV() {
	static const UInt32 retnAddr = 0x70E0A3;
	__asm
	{
		push ecx // keep the address for later
	checkControl:
		mov    ecx, g_DIHookCtrl
		cmp    byte ptr[ecx + 0xCF], 0 // check left control
		jne    checkOtherKeys
		cmp    byte ptr[ecx + 0x44F], 0 // check right control
		je    handleNormalInput

	checkOtherKeys:

	checkV:
		cmp eax, 0x76 // 'v' in ascii
		je handleV
	checkC:
		cmp eax, 0x63 // 'c' in ascii
		je handleC
	checkX:
		cmp eax, 0x78
		je handleX
	checkBackSpace:
		cmp eax, 0x80000000
		je handleBack
	checkLeftArrow:
		cmp eax, 0x80000001
		je handleLeft
	checkRightArrow:
		cmp eax, 0x80000002
		je handleRight
	checkDelete:
		cmp eax, 0x80000007
		jne handleNormalInput

	handleDelete:
		call DeleteNextWord
		jmp done

	handleV:
		call PrintClipBoardToConsoleInput
		jmp done
	handleC:
		call copyInputToClipboard
		jmp done
	handleX:
		call clearInputString
		jmp done
	handleBack:
		call DeletePreviousWord
		jmp done
	handleLeft:
		call MoveToStartOfWord
		jmp done
	handleRight:
		call MoveToEndOfWord
		jmp done

	handleNormalInput:
		pop ecx
		call sendCharToInput
		jmp retnAddr
	done:
		pop ecx
		jmp retnAddr
	}
}


bool versionCheck(const NVSEInterface* nvse) {
	if (nvse->isEditor) return false;
	if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525) {
		_ERROR("incorrect runtime version (got %08X need at least %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
		return false;
	}
	return true;
}
