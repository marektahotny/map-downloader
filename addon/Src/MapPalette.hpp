#ifndef PILON_MAPPALETTE_HPP
#define PILON_MAPPALETTE_HPP

#include "APIEnvir.h"
#include "ACAPinc.h"
#include "DGModule.hpp"

#include <string>

class MapPalette final : public DG::Palette,
						 public DG::PanelObserver
{
public:
	static const GSType	CommandId = 'PMDR';

	virtual ~MapPalette ();

	static bool			HasInstance ();
	static void			CreateInstance ();
	static MapPalette&	GetInstance ();

	void Show ();
	void Hide ();

	void SendResponse (const std::string& json);
	void SendProgress (const std::string& message);

	static GSErrCode				RegisterPaletteControlCallBack ();
	static GSErrCode __ACENV_CALL	RequestCommandHandler (GSHandle, GSPtr, bool);

protected:
	enum { BrowserId = 1 };

	DG::Browser browser;

	MapPalette ();

	void InitBrowserControl ();
	void RegisterACAPIJavaScriptObject ();
	void SetMenuItemCheckedState (bool);

	virtual void PanelResized (const DG::PanelResizeEvent& ev) override;
	virtual void PanelCloseRequested (const DG::PanelCloseRequestEvent& ev, bool* accepted) override;

	static GSErrCode __ACENV_CALL PaletteControlCallBack (Int32 paletteId, API_PaletteMessageID messageID, GS::IntPtr param);

	static GS::Ref<MapPalette> instance;
};

#endif
