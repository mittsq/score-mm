#include "SigScan.h"
#include "helpers.h"
#include <algorithm>
#include <cmath>
#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <implot.h>
#include <iostream>
#include <vector>

#ifdef __INTELLISENSE__
#pragma diag_suppress 651
#endif

SIG_SCAN (sigHitState, 0x14026BC3C,
		  "\xE8\x00\x00\x00\x00\x48\x8B\x4D\xE8\x89\x01", "x????xxxxxx");
SIG_SCAN (sigHitStateInternal, 0x14026D3D0, "\x66\x44\x89\x4C\x24\x00\x53",
		  "xxxxx?x");

extern LRESULT ImGui_ImplWin32_WndProcHandler (HWND hWnd, UINT msg,
											   WPARAM wParam, LPARAM lParam);

ID3D11DeviceContext *pContext = NULL;
ID3D11RenderTargetView *mainRenderTargetView = NULL;
WNDPROC oWndProc;

LRESULT __stdcall WndProc (const HWND hWnd, UINT uMsg, WPARAM wParam,
						   LPARAM lParam) {
	if (ImGui_ImplWin32_WndProcHandler (hWnd, uMsg, wParam, lParam))
		return true;
	if (ImGui::GetCurrentContext () != 0 && ImGui::GetIO ().WantCaptureMouse) {
		switch (uMsg) {
		case WM_LBUTTONDOWN:
		case WM_LBUTTONDBLCLK:
		case WM_LBUTTONUP:
		case WM_RBUTTONDOWN:
		case WM_RBUTTONDBLCLK:
		case WM_RBUTTONUP:
		case WM_MOUSEWHEEL:
			return true;
		}
	}

	return CallWindowProc (oWndProc, hWnd, uMsg, wParam, lParam);
}

typedef enum : i32 {
	Cool = 0,
	Fine = 1,
	Safe = 2,
	Bad = 3,
	Wrong_Red = 4,
	Wrong_Grey = 5,
	Wrong_Green = 6,
	Wrong_Blue = 7,
	Miss = 8,
	NA = 21,
} hitState;

const int RECENT_SIZE = 40;
float timings[RECENT_SIZE];
hitState ratings[RECENT_SIZE];

float lastTiming = 0.0f;
bool sliding = false;
i32 cools = 0;
i32 fines = 0;
i32 safes = 0;
i32 bads = 0;
i32 wrongs = 0;
i32 misses = 0;
llui timingIndex = 0;

ImColor safeColour = ImColor (252, 54, 110, 184);
ImColor fineColour = ImColor (0, 251, 55, 184);
ImColor coolColour = ImColor (94, 241, 251, 184);

const int corner = 0;
bool showHistogram = false;
float totalSum = 0.0f;
int notes = 0;
std::vector<float> allTimings;
std::vector<float> totalAverages;
std::vector<float> runningAverages;

float average (float *arr, i32 size);

HOOK (hitState, __fastcall, CheckHitState,
	  (u64)sigHitState ()
		  + readUnalignedU32 ((void *)((u64)sigHitState () + 1)) + 5,
	  void *a1, bool *a2, void *a3, void *a4, i32 a5, void *a6,
	  u32 *multiCount, u32 *a8, i32 *a9, bool *a10, bool *slide,
	  bool *slide_chain, bool *slide_chain_start, bool *slide_chain_max,
	  bool *slide_chain_continues) {
	sliding = *slide;
	hitState result = originalCheckHitState (
		a1, a2, a3, a4, a5, a6, multiCount, a8, a9, a10, slide, slide_chain,
		slide_chain_start, slide_chain_max, slide_chain_continues);
	if (*slide_chain_continues || sliding)
		return result;
	switch (result) {
	case Cool:
		cools++;
		break;
	case Fine:
		fines++;
		break;
	case Safe:
		safes++;
		break;
	case Bad:
		bads++;
		break;
	case Wrong_Red:
	case Wrong_Grey:
	case Wrong_Green:
	case Wrong_Blue:
		wrongs++;
		break;
	case Miss:
		misses++;
		break;
	case NA:
		break;
	}
	if (result >= Bad)
		return result;

	timings[timingIndex] = lastTiming;
	ratings[timingIndex] = result;
	timingIndex++;
	if (timingIndex >= COUNTOFARR (timings) - 1)
		timingIndex = 0;

	auto scaled = lastTiming * -1000;
	totalSum += scaled;
	notes++;
	allTimings.push_back (scaled);
	totalAverages.push_back (totalSum / notes);

	if (notes < RECENT_SIZE) {
		runningAverages.push_back (totalSum / notes);
	} else {
		runningAverages.push_back (average (timings, RECENT_SIZE) * -1000);
	}

	return result;
}

HOOK (hitState, __stdcall, CheckHitStateInternal, sigHitStateInternal (),
	  void *a1, void *a2, u16 a3, u16 a4) {
	hitState result = originalCheckHitStateInternal (a1, a2, a3, a4);
	if (result >= Bad || sliding)
		return result;
	lastTiming = *(float *)((u64)a2 + 0x18) - *(float *)((u64)a1 + 0x13264);
	return result;
}

float
average (float *arr, i32 size) {
	float sum = 0.0f;
	i32 count = 0;
	for (i32 i = 0; i < size; i++) {
		if (arr[i] > 0.1f)
			continue;
		sum += arr[i];
		count++;
	}
	if (count == 0)
		return 0.0f;
	return sum / count;
}

float
weirdnessToWindow (float weirdness, float min, float max) {
	float sensible = weirdness * -1.0f;
	sensible += 0.10;
	sensible *= 5;
	float difference = max - min;
	float scaled = sensible * difference;
	return scaled + min;
}

int
getBins () {
	auto maxP = std::max_element (allTimings.begin (), allTimings.end ());
	auto minP = std::min_element (allTimings.begin (), allTimings.end ());
	return (int)(std::ceil (*maxP) - std::floor (*minP));
}

#ifdef __cplusplus
extern "C" {
#endif
__declspec(dllexport) void init () {
	INSTALL_HOOK (CheckHitState);
	INSTALL_HOOK (CheckHitStateInternal);
	for (llui i = 0; i < COUNTOFARR (timings); i++) {
		timings[i] = 1.0f;
		ratings[i] = NA;
	}
	toml_table_t *config = openConfig ((char *)"config.toml");
	if (!config)
		return;
	toml_table_t *safeColourSection
		= openConfigSection (config, (char *)"safeColour");
	int r, g, b, a = 0;
	if (safeColourSection) {
		r = readConfigInt (safeColourSection, (char *)"r", 252);
		g = readConfigInt (safeColourSection, (char *)"g", 54);
		b = readConfigInt (safeColourSection, (char *)"b", 110);
		a = readConfigInt (safeColourSection, (char *)"a", 184);
		safeColour = ImColor (r, g, b, a);
	}
	toml_table_t *fineColourSection
		= openConfigSection (config, (char *)"fineColour");
	if (fineColourSection) {
		r = readConfigInt (fineColourSection, (char *)"r", 0);
		g = readConfigInt (fineColourSection, (char *)"g", 251);
		b = readConfigInt (fineColourSection, (char *)"b", 55);
		a = readConfigInt (fineColourSection, (char *)"a", 184);
		fineColour = ImColor (r, g, b, a);
	}
	toml_table_t *coolColourSection
		= openConfigSection (config, (char *)"coolColour");
	if (coolColourSection) {
		r = readConfigInt (coolColourSection, (char *)"r", 94);
		g = readConfigInt (coolColourSection, (char *)"g", 241);
		b = readConfigInt (coolColourSection, (char *)"b", 251);
		a = readConfigInt (coolColourSection, (char *)"a", 184);
		coolColour = ImColor (r, g, b, a);
	}
}

__declspec(dllexport) void D3DInit (IDXGISwapChain *swapChain,
									ID3D11Device *device,
									ID3D11DeviceContext *deviceContext) {
	pContext = deviceContext;

	DXGI_SWAP_CHAIN_DESC sd;
	swapChain->GetDesc (&sd);
	ID3D11Texture2D *pBackBuffer;
	swapChain->GetBuffer (0, __uuidof(ID3D11Texture2D),
						  (LPVOID *)&pBackBuffer);
	device->CreateRenderTargetView (pBackBuffer, NULL, &mainRenderTargetView);
	pBackBuffer->Release ();
	HWND window = sd.OutputWindow;
	oWndProc
		= (WNDPROC)SetWindowLongPtrA (window, GWLP_WNDPROC, (LONG_PTR)WndProc);
	ImGui::CreateContext ();
	ImPlot::CreateContext ();
	ImGuiIO &io = ImGui::GetIO ();
	io.ConfigFlags = ImGuiConfigFlags_NoMouseCursorChange;
	ImGui_ImplWin32_Init (window);
	ImGui_ImplDX11_Init (device, pContext);
}

void
OpenHistogramWindow (bool *checkbox, float average) {
	ImGui::SetNextWindowPos (ImVec2 (400, 400), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize (ImVec2 (640, 480), ImGuiCond_FirstUseEver);
	ImGuiWindowFlags flags = ImGuiWindowFlags_None;
	if (ImGui::Begin ("Stats Window", checkbox, flags)) {
		ImGuiTabBarFlags flags = ImGuiTabBarFlags_NoCloseWithMiddleMouseButton;
		if (ImGui::BeginTabBar ("##tabs", flags)) {
			auto plotFlags = ImPlotFlags_Crosshairs | ImPlotFlags_NoMenus
							 | ImPlotFlags_AntiAliased;
			if (ImGui::BeginTabItem ("Histogram")) {
				if (ImPlot::BeginPlot ("##TimingHistogram", ImVec2 (-1, 0),
									   ImPlotFlags_NoLegend | plotFlags)) {
					ImPlot::PlotHistogram (
						"##HistogramPlot", allTimings.data (),
						allTimings.size (), getBins (), false, false);
					ImPlot::PlotVLines ("Average", &average, 1);
					ImPlot::EndPlot ();
				}
				ImGui::EndTabItem ();
			}
			if (ImGui::BeginTabItem ("Averages")) {
				if (ImPlot::BeginPlot ("##TotalAvPlot", ImVec2 (-1, 0),
									   plotFlags)) {
					ImPlot::SetupLegend (ImPlotLocation_North,
										 ImPlotLegendFlags_Outside
											 | ImPlotLegendFlags_Horizontal);
					auto axes = ImPlotAxisFlags_AutoFit;
					ImPlot::SetupAxes (nullptr, nullptr, axes, axes);
					ImPlot::PlotHLines ("##Average", &average, 1);
					ImPlot::PlotLine ("Total", totalAverages.data (),
									  totalAverages.size ());
					ImPlot::PlotLine ("Running", runningAverages.data (),
									  runningAverages.size ());
					ImPlot::EndPlot ();
				}
				ImGui::EndTabItem ();
			}
			ImGui::EndTabBar ();
		}
	}
	ImGui::End ();
}

__declspec(dllexport) void onFrame (IDXGISwapChain *chain) {
	ImGui_ImplDX11_NewFrame ();
	ImGui_ImplWin32_NewFrame ();
	ImGui::NewFrame ();

	ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
									/* | ImGuiWindowFlags_NoSavedSettings */
									| ImGuiWindowFlags_NoFocusOnAppearing
									| ImGuiWindowFlags_NoNav;
	if (corner != -1) {
		const float PAD = 10.0f;
		const ImGuiViewport *viewport = ImGui::GetMainViewport ();
		ImVec2 work_pos = viewport->WorkPos;
		ImVec2 work_size = viewport->WorkSize;
		ImVec2 window_pos, window_pos_pivot;
		window_pos.x = (corner & 1) ? (work_pos.x + work_size.x - PAD)
									: (work_pos.x + PAD);
		window_pos.y = (corner & 2) ? (work_pos.y + work_size.y - PAD)
									: (work_pos.y + PAD);
		window_pos_pivot.x = (corner & 1) ? 1.0f : 0.0f;
		window_pos_pivot.y = (corner & 2) ? 1.0f : 0.0f;
		ImGui::SetNextWindowSize (ImVec2 (work_size.x / 3.5, work_size.y / 20),
								  ImGuiCond_Always);
		ImGui::SetNextWindowPos (window_pos, ImGuiCond_FirstUseEver,
								 window_pos_pivot);
		// window_flags |= ImGuiWindowFlags_NoMove;
	}
	ImGui::SetNextWindowBgAlpha (0.5f);
	if (ImGui::Begin ("Judgements", 0, window_flags)) {
		auto label = notes == 0 ? 0.0f : totalAverages.back ();
		ImGui::AlignTextToFramePadding ();
		ImGui::Text ("%.0f ms", label);
		ImGui::SameLine ();
		if (ImGui::Button ("Reset")) {
			showHistogram = false;
			cools = 0;
			fines = 0;
			safes = 0;
			bads = 0;
			wrongs = 0;
			misses = 0;
			for (llui i = 0; i < COUNTOFARR (timings); i++) {
				timings[i] = 0.0f;
				ratings[i] = NA;
			}
			timingIndex = 0;
			totalSum = 0.0f;
			notes = 0;
			allTimings.clear ();
			totalAverages.clear ();
			runningAverages.clear ();
		}
		ImGui::SameLine ();
		if (notes == 0)
			ImGui::BeginDisabled ();
		ImGui::Checkbox ("Show stats", &showHistogram);
		if (notes == 0)
			ImGui::EndDisabled ();
		if (showHistogram) {
			OpenHistogramWindow (&showHistogram, totalAverages.back ());
		}
		ImGui::SameLine (ImGui::GetWindowWidth () - 80);
		ImGui::Text ("FPS: %.0f", ImGui::GetIO ().Framerate);

		ImDrawList *draw_list = ImGui::GetWindowDrawList ();
		ImVec2 p = ImGui::GetCursorScreenPos ();
		float startX = p.x + 4.0f;
		float startY = p.y + 4.0f;
		ImVec2 max = ImGui::GetWindowContentRegionMax ();
		float endX = startX + (max.x - 16.0f);
		float endY = startY + (max.y - 36.0f);

		float horizontalStartY = startY + (max.y - 36.0f) / 3.0f;
		float horizontalEndY = horizontalStartY + (max.y - 36.0f) / 2.0f;

		float blueStartX = weirdnessToWindow (0.07f, startX, endX);
		float blueEndX = weirdnessToWindow (-0.07f, startX, endX);

		float greenStartX = weirdnessToWindow (0.03f, startX, endX);
		float greenEndX = weirdnessToWindow (-0.03f, startX, endX);

		float middleX = weirdnessToWindow (0.0f, startX, endX);
		float mean = average (timings, COUNTOFARR (timings));
		float meanX = weirdnessToWindow (mean, startX, endX);
		float leftOfMeanX = weirdnessToWindow (mean + 0.0025f, startX, endX);
		float rightOMeanX = weirdnessToWindow (mean - 0.0025f, startX, endX);

		draw_list->AddRectFilled (ImVec2 (startX, horizontalStartY),
								  ImVec2 (endX, horizontalEndY), safeColour);
		draw_list->AddRectFilled (ImVec2 (blueStartX, horizontalStartY),
								  ImVec2 (blueEndX, horizontalEndY),
								  fineColour);
		draw_list->AddRectFilled (ImVec2 (greenStartX, horizontalStartY),
								  ImVec2 (greenEndX, horizontalEndY),
								  coolColour);

		for (llui i = 0; i < COUNTOFARR (timings); i++) {
			if (timings[i] > 0.15f)
				continue;

			float position = weirdnessToWindow (timings[i], startX, endX);
			ImColor colour;
			switch (ratings[i]) {
			case Cool:
				colour = coolColour;
				break;
			case Fine:
				colour = fineColour;
				break;
			case Safe:
				colour = safeColour;
				break;
			default:
				colour = safeColour;
			}
			draw_list->AddLine (ImVec2 (position, startY),
								ImVec2 (position, endY), colour);
		}

		draw_list->AddLine (ImVec2 (middleX, startY), ImVec2 (middleX, endY),
							ImColor (255, 255, 255, 255));
		draw_list->AddTriangleFilled (
			ImVec2 (leftOfMeanX, startY), ImVec2 (rightOMeanX, startY),
			ImVec2 (meanX, horizontalStartY), ImColor (255, 255, 255, 255));
	}

	ImGui::End ();

	// ImGui::SetNextWindowSize (ImVec2 (110, 160), ImGuiCond_FirstUseEver);
	// ImGui::SetNextWindowPos (ImVec2 (0, 0), ImGuiCond_FirstUseEver);
	// if (ImGui::Begin ("Scores", 0, 0)) {
	// 	ImGui::Text ("Cool: %d", cools);
	// 	ImGui::Text ("Fine: %d", fines);
	// 	ImGui::Text ("Safe: %d", safes);
	// 	ImGui::Text ("Bad: %d", bads);
	// 	ImGui::Text ("Wrong: %d", wrongs);
	// 	ImGui::Text ("Miss: %d", misses);
	// 	if (ImGui::Button ("Reset")) {
	// 		cools = 0;
	// 		fines = 0;
	// 		safes = 0;
	// 		bads = 0;
	// 		wrongs = 0;
	// 		misses = 0;
	// 		for (int i = 0; i < COUNTOFARR (timings); i++) {
	// 			timings[i] = 1.0f;
	// 			ratings[i] = NA;
	// 		}
	// 	}
	// }
	// ImGui::End ();

	ImGui::EndFrame ();
	ImGui::Render ();
	pContext->OMSetRenderTargets (1, &mainRenderTargetView, NULL);
	ImGui_ImplDX11_RenderDrawData (ImGui::GetDrawData ());
}
#ifdef __cplusplus
}
#endif
