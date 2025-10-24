#define TESLA_INIT_IMPL // If you have more than one file using the tesla header, only define this in the main one
#include <tesla.hpp>    // The Tesla Header
#include "SaltyNX.h"
#include <dirent.h>

bool _isDocked = false;
bool _def = true;
bool PluginRunning = false;
bool state = false;
bool closed = false;
bool check = false;
bool SaltySD = false;
bool bak = false;
bool plugin = true;
char saveChar[32];
char DockedChar[32];
char SystemChar[32];
char HandheldDDR[32];
char DockedDDR[32];
uint64_t PID = 0;
Handle remoteSharedMemory = 1;
SharedMemory _sharedmemory = {};
bool SharedMemoryUsed = false;

enum res_mode {
	res_mode_default = 0,
	res_mode_480p = 1,
	res_mode_540p = 2,
	res_mode_630p = 3,
	res_mode_720p = 4,
	res_mode_810p = 5,
	res_mode_900p = 6,
	res_mode_1080p = 7,
	res_mode_amount = 8
};

std::pair<int, int> resolutions[] = {{0 ,0}, {854, 480}, {960, 540}, {1120, 630}, {1280, 720}, {1440, 810}, {1600, 900}, {1920, 1080}};

struct Shared {
	uint32_t MAGIC;
	bool isDocked;
	bool def;
	bool pluginActive;
	struct {
		res_mode handheld_res: 4;
		res_mode docked_res: 4;
	} NX_PACKED res;
	bool wasDDRused;
} NX_PACKED;

static_assert(sizeof(Shared) == 9);

Shared* ReverseNX_RT;

bool writeSave() {
	uint64_t titid = 0;
	if (R_FAILED(pmdmntGetProgramId(&titid, PID))) {
		return false;
	}
	char path[128];
	DIR* dir = opendir("sdmc:/SaltySD/plugins/ReverseNX-RT/");
	if (!dir) {
		mkdir("sdmc:/SaltySD/plugins/", 777);
		mkdir("sdmc:/SaltySD/plugins/ReverseNX-RT/", 777);
	}
	else closedir(dir);
	snprintf(path, sizeof(path), "sdmc:/SaltySD/plugins/ReverseNX-RT/%016lX.dat", titid);
	if (_def) {
		remove(path);
		return true;
	}
	FILE* save_file = fopen(path, "wb");
	if (!save_file)
		return false;
	fprintf(save_file, "NXRT");
	uint8_t version = 2;
	fwrite(&version, 1, 1, save_file);
	fwrite(&_isDocked, 1, 1, save_file);
	uint8_t resolutionModeH = (uint8_t)(ReverseNX_RT->res.handheld_res);
	uint8_t resolutionModeD = (uint8_t)(ReverseNX_RT->res.docked_res);
	fwrite(&resolutionModeH, 1, 1, save_file);
	fwrite(&resolutionModeD, 1, 1, save_file);
	fclose(save_file);
	return true;
}

bool LoadSharedMemory() {
	if (SaltySD_Connect())
		return false;

	SaltySD_GetSharedMemoryHandle(&remoteSharedMemory);
	SaltySD_Term();

	shmemLoadRemote(&_sharedmemory, remoteSharedMemory, 0x1000, Perm_Rw);
	if (!shmemMap(&_sharedmemory)) {
		SharedMemoryUsed = true;
		return true;
	}
	return false;
}

ptrdiff_t searchSharedMemoryBlock(uintptr_t base) {
	ptrdiff_t search_offset = 0;
	while(search_offset < 0x1000) {
		uint32_t* MAGIC_shared = (uint32_t*)(base + search_offset);
		if (*MAGIC_shared == 0x5452584E) {
			return search_offset;
		}
		else search_offset += 4;
	}
	return -1;
}

bool CheckPort () {
	Handle saltysd;
	for (int i = 0; i < 67; i++) {
		if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
			svcCloseHandle(saltysd);
			break;
		}
		else {
			if (i == 66) return false;
			svcSleepThread(1'000'000);
		}
	}
	for (int i = 0; i < 67; i++) {
		if (R_SUCCEEDED(svcConnectToNamedPort(&saltysd, "InjectServ"))) {
			svcCloseHandle(saltysd);
			return true;
		}
		else svcSleepThread(1'000'000);
	}
	return false;
}

class ResolutionModeMenu : public tsl::Gui {
public:
	bool _isDocked = false;
	ResolutionModeMenu (bool isDocked) {
		_isDocked = isDocked;
	}

	// Called when this Gui gets loaded to create the UI
	// Allocate all elements on the heap. libtesla will make sure to clean them up when not needed anymore
	virtual tsl::elm::Element* createUI() override {
		// A OverlayFrame is the base element every overlay consists of. This will draw the default Title and Subtitle.
		// If you need more information in the header or want to change it's look, use a HeaderOverlayFrame.
		auto frame = new tsl::elm::OverlayFrame("ReverseNX-RT", _isDocked ? "更改底座模式默认显示分辨率" : "更改手持模式默认显示分辨率");

		// A list that can contain sub elements and handles scrolling
		auto list = new tsl::elm::List();

		auto *clickableListItem2 = new tsl::elm::ListItem("默认");
		clickableListItem2->setClickListener([this](u64 keys) { 
			if ((keys & HidNpadButton_A) && PluginRunning) {
				if (_isDocked) ReverseNX_RT->res.docked_res = res_mode_default;
				else ReverseNX_RT->res.handheld_res = res_mode_default;
				tsl::goBack();
				return true;
			}
			return false;
		});

		list->addItem(clickableListItem2);
		
		for (uint32_t i = 1; i < res_mode_amount; i++) {
			char Hz[] = "1920x1080";
			snprintf(Hz, sizeof(Hz), "%dx%d", resolutions[i].first, resolutions[i].second);
			auto *clickableListItem = new tsl::elm::ListItem(Hz);
			clickableListItem->setClickListener([this, i](u64 keys) { 
				if ((keys & HidNpadButton_A) && PluginRunning) {
					if (_isDocked) ReverseNX_RT->res.docked_res = (res_mode)i;
					else ReverseNX_RT->res.handheld_res = (res_mode)i;
					tsl::goBack();
					return true;
				}
				return false;
			});

			list->addItem(clickableListItem);
		}

		frame->setContent(list);

		return frame;
	}

	// Called once every frame to handle inputs not handled by other UI elements
	virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
		if (!PluginRunning) {
			tsl::goBack();
			return true;
		}
		return false;   // Return true here to singal the inputs have been consumed
	}
};

class GuiTest : public tsl::Gui {
public:
	GuiTest(u8 arg1, u8 arg2, bool arg3) {}

	// Called when this Gui gets loaded to create the UI
	// Allocate all elements on the heap. libtesla will make sure to clean them up when not needed anymore
	virtual tsl::elm::Element* createUI() override {
		// A OverlayFrame is the base element every overlay consists of. This will draw the default Title and Subtitle.
		// If you need more information in the header or want to change it's look, use a HeaderOverlayFrame.
		auto frame = new tsl::elm::OverlayFrame("ReverseNX-RT", APP_VERSION);

		// A list that can contain sub elements and handles scrolling
		auto list = new tsl::elm::List();
		
		list->addItem(new tsl::elm::CustomDrawer([](tsl::gfx::Renderer *renderer, s32 x, s32 y, s32 w, s32 h) {
			if (!SaltySD) {
				renderer->drawString("SaltyNX 未工作！", false, x, y+50, 20, renderer->a(0xF33F));
			}
			else if (!check) {
				if (closed) {
					renderer->drawString("游戏已退出！插件已禁用！", false, x, y+20, 19, renderer->a(0xF33F));
				}
				else {
					renderer->drawString("游戏未运行！插件已禁用！", false, x, y+20, 19, renderer->a(0xF33F));
				}
			}
			else if (!PluginRunning) {
				renderer->drawString("游戏正在运行", false, x, y+20, 20, renderer->a(0xFFFF));
				renderer->drawString("ReverseNX-RT 未运行！", false, x, y+40, 20, renderer->a(0xF33F));
			}
			else {
				renderer->drawString("ReverseNX-RT 正在运行", false, x, y+20, 20, renderer->a(0xFFFF));
				if (!(ReverseNX_RT->pluginActive)) renderer->drawString("游戏未检查到相关模式！", false, x, y+40, 18, renderer->a(0xF33F));
				else {
					renderer->drawString(SystemChar, false, x, y+42, 20, renderer->a(0xFFFF));
					renderer->drawString(DockedChar, false, x, y+64, 20, renderer->a(0xFFFF));
					if (!(ReverseNX_RT->def)) {
						if (ReverseNX_RT->wasDDRused) {
							renderer->drawString(HandheldDDR, false, x, y+86, 20, renderer->a(0xFFFF));
							renderer->drawString(DockedDDR, false, x, y+108, 20, renderer->a(0xFFFF));
						}
						else {
							renderer->drawString("默认分辨率", false, x, y+86, 20, renderer->a(0xFFFF));
							renderer->drawString("未检测！", false, x, y+108, 20, renderer->a(0xFFFF));							
						}
					}
				}
				renderer->drawString(saveChar, false, x, y+130, 20, renderer->a(0xFFFF));
			}
	}), 150);

		if (PluginRunning && ReverseNX_RT->pluginActive) {

			auto *clickableListItem = new tsl::elm::ListItem("更改系统控制");
			clickableListItem->setClickListener([](u64 keys) { 
				if ((keys & HidNpadButton_A) && PluginRunning) {
					ReverseNX_RT->def = !(ReverseNX_RT->def);
					tsl::goBack();
					tsl::changeTo<GuiTest>(1, 2, true);
					return true;
				}

				return false;
			});

			list->addItem(clickableListItem);

			if (!(ReverseNX_RT->def)) {

				auto *clickableListItem2 = new tsl::elm::ListItem("切换模式");
				clickableListItem2->setClickListener([](u64 keys) { 
					if ((keys & HidNpadButton_A) && PluginRunning) {
						ReverseNX_RT->isDocked = !(ReverseNX_RT->isDocked);
						return true;
					}
					
					return false;
				});
				list->addItem(clickableListItem2);

				if (ReverseNX_RT->wasDDRused) {
					auto *clickableListItem3 = new tsl::elm::ListItem("切换到手持分辨率");
					clickableListItem3->setClickListener([](u64 keys) { 
						if ((keys & HidNpadButton_A) && PluginRunning) {
							tsl::changeTo<ResolutionModeMenu>(false);
							return true;
						}
						
						return false;
					});
					list->addItem(clickableListItem3);

					auto *clickableListItem4 = new tsl::elm::ListItem("切换到底座分辨率");
					clickableListItem4->setClickListener([](u64 keys) { 
						if ((keys & HidNpadButton_A) && PluginRunning) {
							tsl::changeTo<ResolutionModeMenu>(true);
							return true;
						}
						
						return false;
					});
					list->addItem(clickableListItem4);
				}
			}

			auto *clickableListItem3 = new tsl::elm::ListItem("保存当前设置");
			clickableListItem3->setClickListener([](u64 keys) { 
				if ((keys & HidNpadButton_A) && PluginRunning) {
					if (writeSave())
						snprintf(saveChar, sizeof(saveChar), "保存设置成功！");
					else snprintf(saveChar, sizeof(saveChar), "保存设置失败！");
					return true;
				}
				
				return false;
			});
			list->addItem(clickableListItem3);
		}

		// Add the list to the frame for it to be drawn
		frame->setContent(list);
        
		// Return the frame to have it become the top level element of this Gui
		return frame;
	}

	// Called once every frame to update values
	virtual void update() override {
		static uint8_t i = 10;
		Result rc = pmdmntGetApplicationProcessId(&PID);
		if (R_FAILED(rc) && PluginRunning) {
			PluginRunning = false;
			check = false;
			closed = true;
		}

		if (PluginRunning) {
			if (i > 9) {
				_def = ReverseNX_RT->def;
				_isDocked = ReverseNX_RT->isDocked;
				i = 0;
				
				if (_def) sprintf(SystemChar, "系统控制： Yes");
				else sprintf(SystemChar, "系统控制： No");

				if (_def) {
					if (_isDocked) sprintf(DockedChar, "模式： 底座");
					else sprintf(DockedChar, "模式： 手持");
				}
				else {
					if (_isDocked) sprintf(DockedChar, "模式： 模拟底座");
					else sprintf(DockedChar, "模式： 模拟手持");
				}

				if (!ReverseNX_RT->res.handheld_res) strcpy(HandheldDDR, "手持分辨率： 默认");
				else snprintf(HandheldDDR, sizeof(HandheldDDR), "手持分辨率： %dx%d", resolutions[ReverseNX_RT->res.handheld_res].first, resolutions[ReverseNX_RT->res.handheld_res].second);
				if (!ReverseNX_RT->res.docked_res) strcpy(DockedDDR, "底座分辨率： 默认");
				else snprintf(DockedDDR, sizeof(DockedDDR), "底座分辨率： %dx%d", resolutions[ReverseNX_RT->res.docked_res].first, resolutions[ReverseNX_RT->res.docked_res].second);
			}
			else i++;
		}
	
	}

	// Called once every frame to handle inputs not handled by other UI elements
	virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
		if (keysDown & HidNpadButton_B) {
			tsl::goBack();
			tsl::goBack();
			return true;
		}
		return false;   // Return true here to singal the inputs have been consumed
	}
};

class Dummy : public tsl::Gui {
public:
	Dummy(u8 arg1, u8 arg2, bool arg3) {}

	// Called when this Gui gets loaded to create the UI
	// Allocate all elements on the heap. libtesla will make sure to clean them up when not needed anymore
	virtual tsl::elm::Element* createUI() override {
		auto frame = new tsl::elm::OverlayFrame("ReverseNX-RT", APP_VERSION);
		return frame;
	}

	// Called once every frame to handle inputs not handled by other UI elements
	virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override {
		tsl::changeTo<GuiTest>(0, 1, true);
		return true;   // Return true here to singal the inputs have been consumed
	}
};

class OverlayTest : public tsl::Overlay {
public:
	// libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys
	virtual void initServices() override {

		tsl::hlp::doWithSmSession([]{
			
			fsdevMountSdmc();
			SaltySD = CheckPort();
			if (!SaltySD) return;

			if (R_FAILED(pmdmntGetApplicationProcessId(&PID))) return;
			check = true;
			
			if(!LoadSharedMemory()) return;

			if (!PluginRunning) {
				uintptr_t base = (uintptr_t)shmemGetAddr(&_sharedmemory);
				ptrdiff_t rel_offset = searchSharedMemoryBlock(base);
				if (rel_offset > -1) {
					ReverseNX_RT = (Shared*)(base + rel_offset);
					PluginRunning = true;
				}		
			}
		
		});
	
	}  // Called at the start to initialize all services necessary for this Overlay
	
	virtual void exitServices() override {
		shmemClose(&_sharedmemory);
		fsdevUnmountDevice("sdmc");
	}  // Callet at the end to clean up all services previously initialized

	virtual void onShow() override {}    // Called before overlay wants to change from invisible to visible state
	
	virtual void onHide() override {}    // Called before overlay wants to change from visible to invisible state

	virtual std::unique_ptr<tsl::Gui> loadInitialGui() override {
		return initially<Dummy>(1, 2, true);  // Initial Gui to load. It's possible to pass arguments to it's constructor like this
	}
};

int main(int argc, char **argv) {
    return tsl::loop<OverlayTest>(argc, argv);
}
