#include "nvse/nvse/PluginAPI.h"
#include "common/IDebugLog.h"
#include "nvse/nvse/nvse_version.h"
#include "nvse/nvse/Hooks_DirectInput8Create.h"
#include "nvse/nvse/SafeWrite.h"
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
char *getConsoleInputString();
bool versionCheck(const NVSEInterface* nvse);

void hookInputLenCheck();
bool CheckHotkeys();
//bool CheckCommandEqual();


/* called in ASM hook
* all have a dummy return true, as required for where is hooked
*/
bool __fastcall PrintClipBoardToConsoleInput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall clearInputString(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool _fastcall HandleWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall copyInputToClipboard(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall ClearConsoleOutput(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);

bool __fastcall ChangeInputColour(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey);

NVSEInterface *SaveNVSE;
DIHookControl *g_DIHookCtrl = NULL;
HMODULE consolePasteHandle;

int g_bReplaceNewLineWithEnter = 0; // 0 -> replace with space
const int MAX_BUFFER_SIZE = 255;

static const UInt32 GetConsoleManager = 0x71B160;
static const UInt32 kConsoleSendInput = 0x71B210;

#define ClearLines(consoleManager) ThisStdCall(0x71E070, consoleManager+1);
#define RefreshConsoleOutput(consoleManager) ThisStdCall(0x71D410, consoleManager);

static char *clipboardText = (char*)malloc(MAX_BUFFER_SIZE);


#define GameHeapAlloc(size) ThisStdCall(0xAA3E40, (void*)0x11F6238, size)
#define GameHeapFree(ptr) ThisStdCall(0xAA4060, (void*)0x11F6238, ptr)


class DebugText
{
public:
	DebugText();
	~DebugText();

	virtual void    Unk_00(void);
	virtual void    Unk_01(UInt32 arg1, UInt32 arg2);
	virtual UInt32    Unk_02(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32    Unk_03(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4);
	virtual void    Unk_04(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32    Unk_05(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual void    Unk_06(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual UInt32    Unk_07(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6, UInt32 arg7);
	virtual UInt32    Unk_08(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual UInt32    Unk_09(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32    Unk_0A(UInt32 arg1);
	virtual void    Unk_0B(UInt32 arg1, UInt32 arg2);

	struct DebugLine
	{
		float            offsetX;    // 00
		float            offsetY;    // 04
		UInt32            isVisible;    // 08
		NiNode            *node;        // 0C
		String            text;        // 10
		float            flt18;        // 18    Always -1.0
		NiColorAlpha    color;        // 1C
	};

	DebugLine        lines[200];        // 0004
	UInt32            unk2264[14];    // 2264

	static DebugText *GetSingleton();
};
DebugText *DebugText::GetSingleton()
{
	return ((DebugText* (*)(bool))0xA0D9E0)(true);
}

DebugText::DebugLine *GetDebugInputLine()
{
	DebugText *debugText = DebugText::GetSingleton();
	if (!debugText) return NULL;
	DebugText::DebugLine *linesPtr = debugText->lines;
	DebugText::DebugLine *result = linesPtr;
	float maxY = linesPtr->offsetY;
	UInt32 counter = 200;
	do
	{
		linesPtr++;
		if (!linesPtr->text.m_data) break;
		if (maxY < linesPtr->offsetY)
		{
			maxY = linesPtr->offsetY;
			result = linesPtr;
		}
	} while (--counter);
	return result;
}

String* GetDebugInput() {
	return &(GetDebugInputLine()->text);
}


void* (__cdecl *_memcpy)(void *destination, const void *source, size_t num) = memcpy;

__declspec(naked) char* __fastcall StrCopy(char *dest, const char *src)
{
	__asm
	{
		push	ebx
		mov		eax, ecx
		test	ecx, ecx
		jz		done
		test	edx, edx
		jz		done
		xor		ebx, ebx
		getSize :
		cmp[edx + ebx], 0
			jz		doCopy
			inc		ebx
			jmp		getSize
			doCopy :
		push	ebx
			push	edx
			push	eax
			call	_memcpy
			add		esp, 0xC
			add		eax, ebx
			mov[eax], 0
			done:
		pop		ebx
			retn
	}
}

struct _String
{
public:
	char		*m_data;
	UInt16		m_dataLen;
	UInt16		m_bufLen;

	bool Set(const char *src);
};

bool _String::Set(const char *src)
{
	if (!src || !*src)
	{
		if (m_data)
		{
			GameHeapFree(m_data);
			m_data = NULL;
		}
		m_dataLen = m_bufLen = 0;
		return true;
	}
	m_dataLen = strlen(src);
	if (m_bufLen < m_dataLen)
	{
		m_bufLen = m_dataLen;
		if (m_data) GameHeapFree(m_data);
		m_data = (char*)GameHeapAlloc(m_dataLen + 1);
	}
	StrCopy(m_data, src);
	return true;
}

struct TextNode
{
	TextNode	*next;
	TextNode	*prev;
	_String		text;
};

struct TextList
{
	TextNode	*first;
	TextNode	*last;
	UInt32		count;
};

class _ConsoleManager
{
public:
	void		*scriptContext;		// 000
	TextList	printedLines;		// 004
	TextList	inputHistory;		// 010
	UInt32		unk01C[574];		// 01C
};


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
	WriteRelJump(0x71CDDF, (UInt32)hookInputLenCheck);
}

__declspec(naked) void hookInputLenCheck() {
	static const UInt32 retnAddr = 0x71CDE5;
	static const UInt32 skipAddr = 0x71CE53;
	__asm {
		cmp eax, 255 // eax contains string length
		jg ignoreInput
	
		mov eax, [ebp - 0x820]
		jmp retnAddr

	ignoreInput :
		jmp skipAddr
	}
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

		mov     eax, [esp + 4]
		cmp eax, 0x09
		jz handleTab

		mov     eax, g_DIHookCtrl
		cmp     byte ptr[eax + 0xCF], 0    // check left control
		jnz     checkHotkeys
		cmp     byte ptr[eax + 0x44F], 0    // check right control
		jz      handleNormalInput

	checkHotkeys :
		mov     eax, [esp + 4]
		cmp     eax, 0x80000000
		jz      handleEnchancedMovement
		cmp     eax, 0x80000001
		jz      handleEnchancedMovement
		cmp     eax, 0x80000002
		jz      handleEnchancedMovement
		cmp     eax, 0x80000007
		jz      handleEnchancedMovement
		or al, 0x20    // Change to lower-case
		cmp     al, 'v'
		jz      handleV
		cmp		al, 'l'
		jz		handleL
		cmp     al, 'c'
		jz      handleC
		cmp		al, 'f'
		jz handleF
		cmp     al, 'x'
		jnz     handleNormalInput
		jmp     clearInputString
	handleEnchancedMovement :
		jmp     HandleWord
	handleV :
		jmp     PrintClipBoardToConsoleInput
	handleL :
		jmp ClearConsoleOutput
	handleC :
		jmp     copyInputToClipboard
	handleF :
		jmp ChangeInputColour
	consoleMenuNotOpen :
		mov eax, [esp + 4] // restore original eax
	handleNormalInput :
		jmp     retnAddr
	
	handleTab: // ignore tab character if ALT is pressed
		mov     eax, g_DIHookCtrl
		cmp     byte ptr[eax + 0x18C], 0    // check left alt (DirectX scancode * 7 + 4)
		jz consoleMenuNotOpen
	ignoreKey:
		xor al, al
		inc al
		ret
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
			if (g_bReplaceNewLineWithEnter) ThisStdCall(kConsoleSendInput, consoleMgr, enterChar);
			else ThisStdCall(kConsoleSendInput, consoleMgr, ' ');
			break;

			/* remove pipe characters */
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

	/* copy clipboard text into buffer */
	strncpy(*buffer, pszText, MAX_BUFFER_SIZE);

	/* Release the lock and clipboard */
	GlobalUnlock(hData);
	CloseClipboard();
}

char *getConsoleInputString() {
	String* str = GetDebugInput();
	return str ? str->m_data : NULL;
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
	while (*barPos && !isalnum(*barPos++)) numChars++;

	return numChars;
}

bool _fastcall HandleWord(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *consoleInput = getConsoleInputString();
	if (!consoleInput || !(*consoleInput)) return true;

	int timesToRepeat = 0;
	if (inKey == backspaceChar || inKey == leftArrowChar)
		timesToRepeat = getCharsSinceSpace(consoleInput);
	else
		timesToRepeat = getCharsTillSpace(consoleInput);

	for (; timesToRepeat > 0; timesToRepeat--) {
		ThisStdCall(kConsoleSendInput, consoleMgr, inKey);
	}
}

bool __fastcall clearInputString(UInt32 *consoleMgr, UInt32 dummyEDX, UInt32 inKey) {
	char *buffer = getConsoleInputString();
	if (!buffer) return true;

	buffer[0] = '\0';
	ThisStdCall(kConsoleSendInput, consoleMgr, rightArrowChar); //any control character would work here

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
