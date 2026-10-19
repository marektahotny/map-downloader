#include "MapPalette.hpp"

#include "Commands.hpp"
#include "Json.hpp"
#include "ResourceIds.hpp"
#include "Util.hpp"
#include "WebUi.hpp"		// generated from Web/index.html (tools/embed_html.py)

#include <deque>
#include <mutex>

static const GS::Guid	paletteGuid ("{7C3A9E21-5B4D-4F8A-9E61-2D0B8C4F1A37}");
static const short		dialogResId = ID_PALETTE;
static const short		menuResId = ID_MENU_STRINGS;
static const short		menuItemIndex = 1;

GS::Ref<MapPalette>		MapPalette::instance;

static std::mutex				gQueueMutex;
static std::deque<std::string>	gQueue;


static std::string JsSafe (const std::string& json)
{
	// JSON is valid JavaScript except for U+2028 / U+2029 and "</script"
	std::string s = Util::ReplaceAll (json, "\xE2\x80\xA8", "\\u2028");
	s = Util::ReplaceAll (s, "\xE2\x80\xA9", "\\u2029");
	return s;
}


static std::string StringFromJs (GS::Ref<DG::JSBase> param)
{
	GS::Ref<DG::JSValue> v = GS::DynamicCast<DG::JSValue> (param);
	if (v != nullptr && v->GetType () == DG::JSValue::STRING)
		return Util::ToUtf8 (v->GetString ());
	// some versions wrap the arguments into an array
	GS::Ref<DG::JSArray> arr = GS::DynamicCast<DG::JSArray> (param);
	if (arr != nullptr && arr->GetItemArray ().GetSize () > 0) {
		GS::Ref<DG::JSValue> first = GS::DynamicCast<DG::JSValue> (arr->GetItemArray ()[0]);
		if (first != nullptr && first->GetType () == DG::JSValue::STRING)
			return Util::ToUtf8 (first->GetString ());
	}
	return std::string ();
}


// --- Class definition --------------------------------------------------------

MapPalette::MapPalette () :
	DG::Palette (ACAPI_GetOwnResModule (), dialogResId, ACAPI_GetOwnResModule (), paletteGuid),
	browser (GetReference (), BrowserId)
{
	Attach (*this);
	BeginEventProcessing ();
	InitBrowserControl ();
}


MapPalette::~MapPalette ()
{
	EndEventProcessing ();
}


bool MapPalette::HasInstance ()
{
	return instance != nullptr;
}


void MapPalette::CreateInstance ()
{
	DBASSERT (!HasInstance ());
	instance = new MapPalette ();
	ACAPI_KeepInMemory (true);
}


MapPalette& MapPalette::GetInstance ()
{
	DBASSERT (HasInstance ());
	return *instance;
}


void MapPalette::Show ()
{
	DG::Palette::Show ();
	SetMenuItemCheckedState (true);
}


void MapPalette::Hide ()
{
	DG::Palette::Hide ();
	SetMenuItemCheckedState (false);
}


void MapPalette::InitBrowserControl ()
{
	RegisterACAPIJavaScriptObject ();
	browser.LoadHTML (GS::UniString (kWebUiHtml, CC_UTF8));
}


void MapPalette::RegisterACAPIJavaScriptObject ()
{
	DG::JSObject* jsACAPI = new DG::JSObject ("ACAPI");

	// ACAPI.Request (jsonString): queues a request; the answer arrives via PMD.onResponse (json)
	jsACAPI->AddItem (new DG::JSFunction ("Request", [] (GS::Ref<DG::JSBase> param) {
		std::string json = StringFromJs (param);
		{
			std::lock_guard<std::mutex> lock (gQueueMutex);
			gQueue.push_back (json);
		}
		static const API_ModulID mdid = { PILON_DEVELOPER_ID, PILON_LOCAL_ID };
		ACAPI_Command_CallFromEventLoop (&mdid, MapPalette::CommandId, 1L, nullptr, true, nullptr);
		return new DG::JSValue (true);
	}));

	jsACAPI->AddItem (new DG::JSFunction ("GetVersion", [] (GS::Ref<DG::JSBase>) {
		return new DG::JSValue (GS::UniString ("Pilon Map Downloader 0.1 (Archicad 23)"));
	}));

	browser.RegisterAsynchJSObject (jsACAPI);
}


void MapPalette::SendResponse (const std::string& json)
{
	std::string js = "window.PMD && window.PMD.onResponse(" + JsSafe (json) + ");";
	browser.ExecuteJS (Util::ToUni (js));
}


void MapPalette::SendProgress (const std::string& message)
{
	std::string js = "window.PMD && window.PMD.onProgress(\"" + JsonValue::Escape (message) + "\");";
	browser.ExecuteJS (Util::ToUni (js));
}


void MapPalette::SetMenuItemCheckedState (bool isChecked)
{
	API_MenuItemRef	itemRef = {};
	GSFlags			itemFlags = {};

	itemRef.menuResID = menuResId;
	itemRef.itemIndex = menuItemIndex;

	ACAPI_Interface (APIIo_GetMenuItemFlagsID, &itemRef, &itemFlags);
	if (isChecked)
		itemFlags |= API_MenuItemChecked;
	else
		itemFlags &= ~API_MenuItemChecked;
	ACAPI_Interface (APIIo_SetMenuItemFlagsID, &itemRef, &itemFlags);
}


void MapPalette::PanelResized (const DG::PanelResizeEvent& ev)
{
	BeginMoveResizeItems ();
	browser.Resize (ev.GetHorizontalChange (), ev.GetVerticalChange ());
	EndMoveResizeItems ();
}


void MapPalette::PanelCloseRequested (const DG::PanelCloseRequestEvent&, bool* accepted)
{
	Hide ();
	*accepted = true;
}


GSErrCode __ACENV_CALL MapPalette::RequestCommandHandler (GSHandle, GSPtr, bool)
{
	while (true) {
		std::string json;
		{
			std::lock_guard<std::mutex> lock (gQueueMutex);
			if (gQueue.empty ())
				break;
			json = gQueue.front ();
			gQueue.pop_front ();
		}

		JsonValue request;
		std::string parseError;
		JsonValue response;
		if (!JsonValue::Parse (json, request, &parseError) || !request.IsObject ()) {
			response = JsonValue::MakeObject ();
			response.Set ("ok", false);
			response.Set ("error", "Neplatná požiadavka: " + parseError);
		} else {
			response = Commands::Execute (request, [] (const std::string& msg) {
				if (MapPalette::HasInstance ())
					MapPalette::GetInstance ().SendProgress (msg);
			});
		}
		response.Set ("id", request["id"]);
		response.Set ("cmd", request["cmd"]);
		if (MapPalette::HasInstance ())
			MapPalette::GetInstance ().SendResponse (response.Serialize ());
	}
	return NoError;
}


GSErrCode __ACENV_CALL MapPalette::PaletteControlCallBack (Int32, API_PaletteMessageID messageID, GS::IntPtr param)
{
	switch (messageID) {
		case APIPalMsg_OpenPalette:
			if (!HasInstance ())
				CreateInstance ();
			GetInstance ().Show ();
			break;

		case APIPalMsg_ClosePalette:
			if (!HasInstance ())
				break;
			GetInstance ().Hide ();
			break;

		case APIPalMsg_HidePalette_Begin:
			if (HasInstance () && GetInstance ().IsVisible ())
				GetInstance ().Hide ();
			break;

		case APIPalMsg_HidePalette_End:
			if (HasInstance () && !GetInstance ().IsVisible ())
				GetInstance ().Show ();
			break;

		case APIPalMsg_DisableItems_Begin:
			if (HasInstance () && GetInstance ().IsVisible ())
				GetInstance ().DisableItems ();
			break;

		case APIPalMsg_DisableItems_End:
			if (HasInstance () && GetInstance ().IsVisible ())
				GetInstance ().EnableItems ();
			break;

		case APIPalMsg_IsPaletteVisible:
			*(reinterpret_cast<bool*> (param)) = HasInstance () && GetInstance ().IsVisible ();
			break;

		default:
			break;
	}
	return NoError;
}


GSErrCode MapPalette::RegisterPaletteControlCallBack ()
{
	return ACAPI_RegisterModelessWindow (
		GS::GenerateHashValue (paletteGuid),
		PaletteControlCallBack,
		API_PalEnabled_FloorPlan + API_PalEnabled_Section + API_PalEnabled_Elevation +
		API_PalEnabled_InteriorElevation + API_PalEnabled_3D + API_PalEnabled_Detail +
		API_PalEnabled_Worksheet + API_PalEnabled_Layout + API_PalEnabled_DocumentFrom3D,
		GSGuid2APIGuid (paletteGuid));
}
