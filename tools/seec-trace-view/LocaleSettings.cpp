//===- tools/seec-trace-view/LocaleSettings.cpp ---------------------------===//
//
//                                    SeeC
//
// This file is distributed under The MIT License (MIT). See LICENSE.TXT for
// details.
//
//===----------------------------------------------------------------------===//
///
/// \file
///
//===----------------------------------------------------------------------===//

#include "seec/ICU/Resources.hpp"
#include "seec/wxWidgets/ImageResources.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include <wx/bitmap.h>
#include <wx/bmpcbox.h>
#include <wx/config.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/stdpaths.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include "LocaleSettings.hpp"

char const * const cConfigKeyForLocaleID = "/Locale/ID";

/// \brief Allows the user to configure locale settings.
///
class LocaleSettingsDlg : public wxDialog
{
  wxBitmapComboBox *Selector;

  std::vector<Locale> AvailableLocales;

public:
  /// \brief Constructor (without creation).
  ///
  LocaleSettingsDlg()
  : Selector(nullptr),
    AvailableLocales()
  {}

  /// \brief Constructor (with creation).
  ///
  LocaleSettingsDlg(wxWindow *Parent)
  : Selector(nullptr),
    AvailableLocales()
  {
    Create(Parent);
  }

  /// \brief Destructor.
  ///
  virtual ~LocaleSettingsDlg()
  {

  }

  /// \brief Create the frame.
  ///
  bool Create(wxWindow *Parent)
  {
    auto const CurrentLocale = getLocale();
    auto const ResTraceViewer = seec::Resource("TraceViewer", CurrentLocale);

    auto const ResText = ResTraceViewer["GUIText"]["LocaleSettingsDialog"];
    if (U_FAILURE(ResText.status()))
      return false;

    auto const Title = seec::towxString(ResText["Title"].asStringOrDefault(""));
    if (!wxDialog::Create(Parent, wxID_ANY, Title)) {
      return false;
    }

    auto const ResFlags = ResTraceViewer["GUIImages"]["CountryFlags"];

    Selector = new wxBitmapComboBox(this,
                                    wxID_ANY,
                                    wxEmptyString,
                                    wxDefaultPosition,
                                    wxSize(300, wxDefaultSize.GetHeight()),
                                    0,
                                    nullptr,
                                    wxCB_READONLY);

    int CurrentLocaleIndex = wxNOT_FOUND;

    int32_t NumLocales = 0;
    if (auto const Locales = icu::Locale::getAvailableLocales(NumLocales)) {
      if (NumLocales > 0) {
        std::string FlagKey;
        UnicodeString DisplayName;

        // Get a "root" flag to use for locales which don't have flags.
        auto const ResRootFlag = ResFlags["root"];
        UErrorCode RootFlagStatus = ResRootFlag.status();
        auto const RootFlag = seec::getwxImage(ResRootFlag.bundle(),
                                               RootFlagStatus);

        for (int32_t i = 0; i < NumLocales; ++i) {
          // Attempt to open the TraceViewer ResourceBundle using this Locale,
          // to check if SeeC has an appropriate translation.
          auto const ResForLocale = seec::Resource("TraceViewer", Locales[i]);

          if (ResForLocale.status() == U_ZERO_ERROR) {
            if (CurrentLocale == Locales[i])
              CurrentLocaleIndex = static_cast<int>(Selector->GetCount());

            Locales[i].getDisplayName(Locales[i], DisplayName);
            AvailableLocales.push_back(Locales[i]);

            FlagKey = Locales[i].getLanguage();
            if (auto const Country = Locales[i].getCountry()) {
              if (*Country) {
                (FlagKey += "_") += Country;
              }
            }

            auto const ResFlag = ResFlags[FlagKey.c_str()];
            UErrorCode Status = ResFlag.status();
            auto const Flag = seec::getwxImage(ResFlag.bundle(), Status);

            if (U_SUCCESS(Status)) {
              Selector->Append(seec::towxString(DisplayName), wxBitmap(Flag));
            }
            else if (U_SUCCESS(RootFlagStatus)) {
              Selector->Append(seec::towxString(DisplayName),
                               wxBitmap(RootFlag));
            }
            else {
              Selector->Append(seec::towxString(DisplayName));
            }
          }
        }
      }
    }

    if (CurrentLocaleIndex != wxNOT_FOUND)
      Selector->SetSelection(CurrentLocaleIndex);

    // Create accept/cancel buttons.
    auto const Buttons = wxDialog::CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    // Vertical sizer to hold each row of input.
    auto const ParentSizer = new wxBoxSizer(wxVERTICAL);
    int const BorderDir = wxLEFT | wxRIGHT;
    int const BorderSize = 5;
    int const InterSettingSpace = 10;

    ParentSizer->Add(Selector, wxSizerFlags().Proportion(1)
                                             .Expand()
                                             .Border(BorderDir | wxTOP,
                                                     BorderSize));

    ParentSizer->AddSpacer(InterSettingSpace);

    ParentSizer->Add(Buttons, wxSizerFlags().Expand()
                                            .Border(BorderDir | wxBOTTOM,
                                                    BorderSize));

    SetSizerAndFit(ParentSizer);

    return true;
  }

  /// \brief Save the current settings into the user's configuration.
  ///
  bool SaveValues()
  {
    auto const Selection = Selector->GetSelection();
    if (Selection == wxNOT_FOUND)
      return false;

    auto const &TheLocale = AvailableLocales[Selection];

    auto const Config = wxConfig::Get();
    Config->Write(cConfigKeyForLocaleID, TheLocale.getName());
    Config->Flush();

    return true;
  }
};

void showLocaleSettings()
{
  LocaleSettingsDlg Dlg(nullptr);

  while (true) {
    auto const Result = Dlg.ShowModal();

    if (Result == wxID_OK)
      if (!Dlg.SaveValues())
        continue;

    break;
  }

  UErrorCode Status = U_ZERO_ERROR;
  icu::Locale::setDefault(getLocale(), Status);
}

icu::Locale getLocale()
{
  auto const Config = wxConfig::Get();

  wxString LocaleID;
  if (Config->Read(cConfigKeyForLocaleID, &LocaleID)) {
    auto const TheLocale = icu::Locale::createFromName(LocaleID);
    if (!TheLocale.isBogus())
      return TheLocale;
  }

  return icu::Locale();
}
