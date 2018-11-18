#include "nvse/nvse/PluginAPI.h"
#include "common/IDebugLog.h"
#include "nvse/nvse/nvse_version.h"
#include "nvse/nvse/Hooks_DirectInput8Create.h"
#include "nvse/nvse/SafeWrite.h"
#include <windows.h>

//IDebugLog gLog("nvse_console_clipboard.log");
HMODULE consolePasteHandle;

/* 0 means replace with space */
int g_bReplaceNewLineWithEnter = 0;

const UInt32 kConsoleSendInput = 0x71B210;

enum
{
	kSpclChar_Backspace = 0x80000000,
	kSpclChar_LeftArrow = 0x80000001,
	kSpclChar_RightArrow = 0x80000002,
	kSpclChar_Delete = 0x80000007,
	kSpclChar_Enter = 0x80000008,
};

DIHookControl *g_DIHookCtrl = NULL;
class _ConsoleManager;

// function prototypes
void handleIniOptions();
void CheckCTRLHotkeys();
bool __fastcall PrintClipBoardToConsoleInput(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall CopyInputToClipboard(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall ClearInputString(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall DeletePreviousWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall DeleteNextWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall MoveToStartOfWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);
bool __fastcall MoveToEndOfWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey);

extern "C"
{
	BOOL WINAPI DllMain(HANDLE hDllHandle, DWORD dwReason, LPVOID lpreserved)
	{
		if (dwReason == DLL_PROCESS_ATTACH)
			consolePasteHandle = (HMODULE)hDllHandle;
		return TRUE;
	}

	bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
	{
		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "Console Clipboard";
		info->version = 1;
		return !nvse->isEditor && (nvse->runtimeVersion == RUNTIME_VERSION_1_4_0_525) && (nvse->nvseVersion >= 0x5010010);
	}

	bool NVSEPlugin_Load(const NVSEInterface *nvse)
	{
		NVSEDataInterface *nvseData = (NVSEDataInterface*)nvse->QueryInterface(kInterface_Data);
		g_DIHookCtrl = (DIHookControl*)nvseData->GetSingleton(NVSEDataInterface::kNVSEData_DIHookControl);

		handleIniOptions();

		WriteRelCall(0x70E09E, (UInt32)CheckCTRLHotkeys);
		return true;
	}

};

void handleIniOptions()
{
	char filename[MAX_PATH];
	GetModuleFileNameA(consolePasteHandle, filename, MAX_PATH);
	strcpy((char *)(strrchr(filename, '\\') + 1), "nvse_console_clipboard.ini");
	g_bReplaceNewLineWithEnter = GetPrivateProfileIntA("Main", "bReplaceNewLineWithEnter", 0, filename);
}

#define GameHeapAlloc(size) ThisStdCall(0xAA3E40, (void*)0x11F6238, size)
#define GameHeapFree(ptr) ThisStdCall(0xAA4060, (void*)0x11F6238, ptr)

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

class _ConsoleManager
{
public:
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

	void		*scriptContext;		// 000
	TextList	printedLines;		// 004
	TextList	inputHistory;		// 010
	UInt32		unk01C[574];		// 01C
};

class DebugText
{
public:
	virtual void	Unk_00(void);
	virtual void	Unk_01(UInt32 arg1, UInt32 arg2);
	virtual UInt32	Unk_02(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32	Unk_03(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4);
	virtual void	Unk_04(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32	Unk_05(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual void	Unk_06(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual UInt32	Unk_07(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6, UInt32 arg7);
	virtual UInt32	Unk_08(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5);
	virtual UInt32	Unk_09(UInt32 arg1, UInt32 arg2, UInt32 arg3, UInt32 arg4, UInt32 arg5, UInt32 arg6);
	virtual UInt32	Unk_0A(UInt32 arg1);
	virtual void	Unk_0B(UInt32 arg1, UInt32 arg2);

	UInt32			unk0004;		// 0004
	UInt32			unk0008;		// 0008
	UInt32			unk000C;		// 000C
	UInt32			unk0010;		// 0010
	_String			currText;		// 0014
	UInt32			unk001C[2208];	// 001C
}
**g_debugText = (DebugText**)0x11F33A8;

/*
EAX will contain the character pressed
ECX contains the location of the ConsoleManager singleton
*/
__declspec(naked) void CheckCTRLHotkeys()
{
	__asm
	{
		mov		eax, g_DIHookCtrl
		cmp		byte ptr[eax + 0xCF], 0	// check left control
		jnz		checkHotkeys
		cmp		byte ptr[eax + 0x44F], 0	// check right control
		jz		handleNormalInput
		checkHotkeys :
		mov		eax, [esp + 4]
			cmp		eax, kSpclChar_Backspace
			jz		handleBack
			cmp		eax, kSpclChar_LeftArrow
			jz		handleLeft
			cmp		eax, kSpclChar_RightArrow
			jz		handleRight
			cmp		eax, kSpclChar_Delete
			jz		handleDelete
			or al, 0x20	// Change to lower-case
			cmp		eax, 'v'
			jz		handleV
			cmp		eax, 'c'
			jz		handleC
			cmp		eax, 'x'
			jz		handleX
			mov		al, 1
			retn	4
			handleBack:
		jmp		DeletePreviousWord
			handleLeft :
		jmp		MoveToStartOfWord
			handleRight :
		jmp		MoveToEndOfWord
			handleDelete :
		jmp		DeleteNextWord
			handleV :
		jmp		PrintClipBoardToConsoleInput
			handleC :
		jmp		CopyInputToClipboard
			handleX :
		jmp		ClearInputString
			handleNormalInput :
		jmp		kConsoleSendInput
	}
}

UInt32 GetCharsSinceSpace()
{
	DebugText *debugText = *g_debugText;
	UInt32 numChars = 0;

	if (debugText && debugText->currText.m_dataLen)
	{
		char *data = debugText->currText.m_data, *barPos = strchr(data, '|');
		if (barPos)
		{
			while (barPos != data && !isalnum(*--barPos)) numChars++;
			while (barPos >= data && isalnum(*barPos--)) numChars++;
		}
	}
	return numChars;
}

UInt32 GetCharsTillSpace()
{
	DebugText *debugText = *g_debugText;
	UInt32 numChars = 0;

	if (debugText && debugText->currText.m_dataLen)
	{
		char *barPos = strchr(debugText->currText.m_data, '|');
		if (barPos)
		{
			while (isalnum(*++barPos)) numChars++;
			while (!isalnum(*barPos++) && *barPos) numChars++;
		}
	}
	return numChars;
}

char s_stringBuffer[0x8000];

bool __fastcall PrintClipBoardToConsoleInput(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	DebugText *debugText = *g_debugText;
	if (debugText)
	{
		char *bufPtr = s_stringBuffer;
		/* Try opening the clipboard */
		if (OpenClipboard(NULL))
		{
			/* Get handle of clipboard object for ANSI text */
			HANDLE hData = GetClipboardData(CF_TEXT);
			if (hData)
			{
				/* Lock the handle to get the actual text pointer */
				char *pszText = static_cast<char *>(GlobalLock(hData));
				if (pszText)
				{
					char chr;
					UInt32 maxChars = 0x4000;
					while (chr = *pszText)
					{
						if (chr == '\n')
						{
							if (g_bReplaceNewLineWithEnter)
							{
								*bufPtr = 0;
								bufPtr = s_stringBuffer;
								debugText->currText.Set(s_stringBuffer);
								ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_Enter);
							}
							else *bufPtr++ = ' ';
						}
						else if ((chr != '\r') && (chr != '|'))
							*bufPtr++ = chr;
						if (!--maxChars) break;
						pszText++;
					}
				}
				GlobalUnlock(hData);
			}
			CloseClipboard();
		}
		*bufPtr = 0;
		debugText->currText.Set(s_stringBuffer);
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_RightArrow);
	}
	return true;
}

bool __fastcall CopyInputToClipboard(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	char *bufPtr = s_stringBuffer;
	UInt32 length = 0;
	for (_ConsoleManager::TextNode *traverse = consoleMgr->printedLines.first; traverse; traverse = traverse->next)
	{
		if (!traverse->text.m_dataLen) continue;
		length += traverse->text.m_dataLen + 1;
		bufPtr = StrCopy(bufPtr, traverse->text.m_data);
		*bufPtr++ = '\n';
	}
	*bufPtr = 0;
	OpenClipboard(NULL);
	EmptyClipboard();
	if (length)
	{
		HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, length + 1);
		if (hg)
		{
			memcpy(GlobalLock(hg), s_stringBuffer, length + 1);
			GlobalUnlock(hg);
			SetClipboardData(CF_TEXT, hg);
			GlobalFree(hg);
		}
	}
	CloseClipboard();
	return true;
}

bool __fastcall ClearInputString(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	DebugText *debugText = *g_debugText;
	if (debugText)
	{
		debugText->currText.Set("");
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_RightArrow);
	}
	return true;
}

bool __fastcall DeletePreviousWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	for (UInt32 numChars = GetCharsSinceSpace(); numChars; --numChars)
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_Backspace);
	return true;
}

bool __fastcall DeleteNextWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	for (UInt32 numChars = GetCharsTillSpace(); numChars; --numChars)
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_Delete);
	return true;
}

bool __fastcall MoveToStartOfWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	for (UInt32 numChars = GetCharsSinceSpace(); numChars; --numChars)
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_LeftArrow);
	return true;
}

bool __fastcall MoveToEndOfWord(_ConsoleManager *consoleMgr, UInt32 dummyEDX, UInt32 inKey)
{
	for (UInt32 numChars = GetCharsTillSpace(); numChars; --numChars)
		ThisStdCall(kConsoleSendInput, consoleMgr, kSpclChar_RightArrow);
	return true;
}
