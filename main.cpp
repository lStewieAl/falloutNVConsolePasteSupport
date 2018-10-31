#include "nvse/nvse/PluginAPI.h"
#include "nvse/nvse/CommandTable.h"
#include "nvse/nvse/GameAPI.h"
#include "nvse/nvse/ParamInfos.h"
#include "nvse/nvse/GameObjects.h"
#include "common/IDebugLog.h"
#include "nvse/nvse/nvse_version.h"
#include "nvse/nvse/Hooks_DirectInput8Create.h"
#include "nvse/nvse/SafeWrite.h"
#include <string>
#include <windows.h>

#ifdef NOGORE
IDebugLog		gLog("nvse_plugin_console_clipboard.log");
#else
IDebugLog		gLog("nvse_plugin_console_clipboard.log");
#endif

PluginHandle	g_pluginHandle = kPluginHandle_Invalid;

NVSEMessagingInterface* g_msg;
NVSEInterface * SaveNVSE;
NVSECommandTableInterface * g_cmdTable;
const CommandInfo * g_TFC;
NVSEScriptInterface* g_script;
DIHookControl *g_DIHookCtrl = NULL;

#define ExtractArgsEx(...) g_script->ExtractArgsEx(__VA_ARGS__)
#define ExtractFormatStringArgs(...) g_script->ExtractFormatStringArgs(__VA_ARGS__)


//function prototypes
void patchOnConsoleInput();
void __fastcall CheckCTRLV();
static const char* GetClipboardText();
void __fastcall PrintToConsoleInput(UInt32 character);

static const UInt32 handleNormal = 0x71B210;

static const UInt32 getConsoleStringLocation = 0x71B160;
static const UInt32 sendCharToInput = 0x71B210;

static char clipboardBuffer[256];



extern "C" {

bool NVSEPlugin_Query(const NVSEInterface * nvse, PluginInfo * info)
{
	_MESSAGE("query");

	// fill out the info structure
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "nvse_plugin_console_clipboard";
	info->version = 2;

	// version checks
	if(nvse->nvseVersion < NVSE_VERSION_INTEGER)
	{
		_ERROR("NVSE version too old (got %08X expected at least %08X)", nvse->nvseVersion, NVSE_VERSION_INTEGER);
		return false;
	}

	if(!nvse->isEditor)
	{
		if(nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525)
		{
			_ERROR("incorrect runtime version (got %08X need at least %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
			return false;
		}

#ifdef NOGORE
		if(!nvse->isNogore)
		{
			_ERROR("incorrect runtime edition (got %08X need %08X (nogore))", nvse->isNogore, 1);
			return false;
		}
#else
		if(nvse->isNogore)
		{
			_ERROR("incorrect runtime edition (got %08X need %08X (standard))", nvse->isNogore, 0);
			return false;
		}
#endif
	}
	else
	{
		if(nvse->editorVersion < CS_VERSION_1_4_0_518)
		{
			_ERROR("incorrect editor version (got %08X need at least %08X)", nvse->editorVersion, CS_VERSION_1_4_0_518);
			return false;
		}
#ifdef NOGORE
		_ERROR("Editor only uses standard edition, closing.");
		return false;
#endif
	}

	// version checks pass

	return true;
}

void MessageHandler(NVSEMessagingInterface::Message* msg)
{
	switch (msg->type)
	{
		case NVSEMessagingInterface::kMessage_PostLoad:
			patchOnConsoleInput();
	}
}


bool NVSEPlugin_Load(const NVSEInterface * nvse)
{
	_MESSAGE("load");

	g_pluginHandle = nvse->GetPluginHandle();

	// save the NVSEinterface in cas we need it later
	SaveNVSE = (NVSEInterface *)nvse;
  
  // register to receive messages from NVSE
	NVSEMessagingInterface* msgIntfc = (NVSEMessagingInterface*)nvse->QueryInterface(kInterface_Messaging);
	msgIntfc->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);
  
  NVSEDataInterface *nvseData = (NVSEDataInterface*)nvse->QueryInterface(kInterface_Data);
	g_DIHookCtrl = (DIHookControl*)nvseData->GetSingleton(NVSEDataInterface::kNVSEData_DIHookControl);

	g_script = (NVSEScriptInterface*)nvse->QueryInterface(kInterface_Script);

	/***************************************************************************
	 *	
	 *	READ THIS!
	 *	
	 *	Before releasing your plugin, you need to request an opcode range from
	 *	the NVSE team and set it in your first SetOpcodeBase call. If you do not
	 *	do this, your plugin will create major compatibility issues with other
	 *	plugins, and will not load in release versions of NVSE. See
	 *	nvse_readme.txt for more information.
	 *	
	 **************************************************************************/

	return true;
}

};


void copyString(const std::string& input, char *dst, size_t dst_size)
{
    strncpy(dst, input.c_str(), dst_size - 1);
    dst[dst_size - 1] = '\0';
}

void patchOnConsoleInput() {
  UInt32 onConsoleInputAddress = 0x70E09E;
	WriteRelJump(onConsoleInputAddress, (UInt32) CheckCTRLV);
}


/*__declspec(naked) */void PrintClipBoardToConsoleInput() {
	char character = 0;
	int i = 0;
	UInt32 c = 0;
	char clipboardText[] = "PLEASE WORK\0";
	
	//clipboardText = GetClipboardText();
   // _MESSAGE("%s", clipboardText);
	

	for(; clipboardText[i] != '\0'; i++) {
      PrintToConsoleInput(clipboardText[i]);
	}
}


static const char* GetClipboardText()
{
  // Try opening the clipboard
	if (! OpenClipboard(NULL)) {
      return NULL;
	}

  // Get handle of clipboard object for ANSI text
  HANDLE hData = GetClipboardData(CF_TEXT);
  if (hData == NULL) {
    return NULL;
  }

  // Lock the handle to get the actual text pointer
  char * pszText = static_cast<char*>( GlobalLock(hData) );
  if (pszText == NULL) {
    return NULL;
  }
  
  // Save text in a string class instance
  std::string text( pszText );

  // Release the lock
  GlobalUnlock( hData );

  // Release the clipboard
  CloseClipboard();

  return text.c_str();
}

void __fastcall PrintToConsoleInput(UInt32 c)
{
	__asm
	{
      getConsoleInputStringLocation:
        push  00
        call  getConsoleStringLocation // sets eax to location of console input String
        push  eax
      sendCharToConsoleInput:
        mov   eax, c
        pop   ecx
        push  eax
        call sendCharToInput
	}
}


/*
  eax will contain the character pressed, v's code is 0x76
  ecx contains the location of the input buffer to write to
*/
__declspec(naked) void __fastcall CheckCTRLV()
{
	static const UInt32 retnAddr = 0x70E0A3;
	__asm
	{
		push ecx
	  checkV:
        cmp eax, 0x76 // compare input to 'v'
        jne handleNormalInput
	  checkControl:	
        mov		ecx, g_DIHookCtrl
		cmp		byte ptr [ecx+0xCF], 0 // check left control
		jne		handlePrint
		cmp		byte ptr [ecx+0x44F], 0 // check right control
		je		handleNormalInput
      handlePrint:
        call PrintClipBoardToConsoleInput
		pop ecx
        jmp done
      handleNormalInput:
		pop ecx
        call handleNormal
      done:
		jmp retnAddr
  }
}
