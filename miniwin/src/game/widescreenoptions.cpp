// [library:config] Localized Video Options row built from copied OPTIONS.MIB
// bindings. It owns its carousel/labels; gameplay selectors and bindings are untouched.
#include "menu/screens/optionsscreen.h"

#include <miniwin/widescreen.h>
#include <miniwin/widescreenoptions.h>
#include <string.h>

// The menu text folder table GameState uses (languages 0 to 8).
extern const LegoChar* g_menuLanguageDirectories[9];

namespace
{
class WidescreenSelector : public MenuSelector {
public:
	void StepPrevious() override
	{
		MenuSelector::StepPrevious();
		Apply();
	}
	void StepNext() override
	{
		MenuSelector::StepNext();
		Apply();
	}

private:
	void Apply()
	{
		bool enabled = m_carousel->GetSelectedIndex() != 0;
		if (enabled != MiniwinWidescreen_Get()) {
			MiniwinWidescreen_Set(enabled);
		}
	}
};

struct VideoRow {
	MenuCarouselNavigator carousel;
	WidescreenSelector selector;
	MenuTextLabel title;
	MenuTextLabel values[2];
	undefined2 titleBuffer[32];

	~VideoRow()
	{
		// Detach the icon while the screen/root and its callback handler still live.
		selector.Destroy();
	}
};

// Owned by the Options screen's lifetime (see Destroy); deliberately not a static
// object so a process exit with the screen still open touches nothing.
VideoRow* g_row = nullptr;

const wchar_t* RowLabel(unsigned int p_languageIndex)
{
	struct Translation {
		const char* folder;
		const wchar_t* text;
	};
	static const Translation labels[] = {
		{"english", L"WIDESCREEN"},
		{"danish", L"BREDSK\u00c6RM"},
		{"dutch", L"BREEDBEELD"},
		{"finnish", L"LAAJAKUVA"},
		{"french", L"\u00c9CRAN LARGE"},
		{"german", L"BREITBILD"},
		{"italian", L"SCHERMO PANORAMICO"},
		{"norwegi", L"BREDSKJERM"},
		{"spanish", L"PANTALLA PANOR\u00c1MICA"},
		{"swedish", L"BREDBILD"},
	};
	// Index 10 (registry LangID 0x000b) is Finnish, which the menu folder table lacks.
	const char* folder = p_languageIndex < 9 ? g_menuLanguageDirectories[p_languageIndex]
											 : (p_languageIndex == 10 ? "finnish" : "english");
	for (const auto& label : labels) {
		if (strcmp(folder, label.folder) == 0) {
			return label.text;
		}
	}
	return labels[0].text;
}
} // namespace

void MiniwinWidescreenOptions_Create(OptionsScreen* p_screen, unsigned int p_languageIndex)
{
	delete g_row;
	g_row = new VideoRow;
	auto& row = *g_row;
	p_screen->CreateCarousel(&row.carousel, 0x3d, 0x3b);

	// Deep-copy the nested selector binding exactly as CreateSelector does. All
	// creation defaults are resolved on our copies; the loaded MIB is never edited.
	auto params = *static_cast<MenuSelector::CreateParams*>(p_screen->GetBindingEntry(0xf4));
	auto prev = *params.m_prevButtonParams;
	auto next = *params.m_nextButtonParams;
	auto frame = *params.m_frameParams;
	params.m_prevButtonParams = &prev;
	params.m_nextButtonParams = &next;
	params.m_frameParams = &frame;
	params.m_carouselWidget = &row.carousel;
	params.m_id = 0x7f00;
	// Reuse the lower Game Options row geometry (below the driver selector).
	auto* style = static_cast<MenuStyleTable::SelectorStyle*>(p_screen->GetStyleEntry(0x4c));
	p_screen->ApplySelectorDefaults(&params, style);
	row.selector.Create(&params, style);

	for (int i = 0; i < 2; i++) {
		// CreateTextLabel uses the current m_menuTextStrings: SRF 116 = NO, 115 = YES.
		p_screen->CreateTextLabel(&row.values[i], 0x96, 0x37, i ? 115 : 116);
		row.carousel.AddItem(&row.values[i]);
	}
	row.carousel.SetSelection(MiniwinWidescreen_Get() ? 1 : 0);
	p_screen->CreateTextLabel(&row.title, 0x101, 0x37, 0x5a);
	// GolString borrows its UTF-16 code-unit buffer, so retain it with the row.
	// These Latin-1 Unicode capitals match the SRF/font glyph codes directly.
	const wchar_t* label = RowLabel(p_languageIndex);
	unsigned int length = 0;
	while (label[length] && length + 1 < sizeof(row.titleBuffer) / sizeof(row.titleBuffer[0])) {
		row.titleBuffer[length] = (undefined2) label[length];
		length++;
	}
	row.titleBuffer[length] = 0;
	GolString string;
	string.CopyFromBufSelection(row.titleBuffer, length + 1);
	row.title.SetString(&string, FALSE);
	MiniwinWidescreenOptions_Show(false);
}

void MiniwinWidescreenOptions_Show(bool p_show)
{
	if (!g_row) {
		return;
	}
	if (p_show) {
		g_row->carousel.SetSelection(MiniwinWidescreen_Get() ? 1 : 0);
		g_row->selector.SetFlags(2);
		g_row->title.SetFlags(2);
	}
	else {
		g_row->selector.ClearFlags(2);
		g_row->title.ClearFlags(2);
	}
}

void MiniwinWidescreenOptions_Destroy()
{
	delete g_row;
	g_row = nullptr;
}
