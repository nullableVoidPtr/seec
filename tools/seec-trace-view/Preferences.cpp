//===- tools/seec-trace-view/Preferences.hpp ------------------------------===//
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
#include "seec/wxWidgets/StringConversion.hpp"

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/frame.h>
#include <wx/listbook.h>
#include <wx/window.h>

#include <vector>

#include "LocaleSettings.hpp"
#include "Preferences.hpp"

class PreferenceDialog final : public wxDialog
{
  /// Holds individual frames of preferences.
  wxBookCtrlBase *m_Book;

  /// Holds pointers to all of our pages.
  std::vector<PreferenceWindow *> m_Pages;

  void AddPage(PreferenceWindow *Page)
  {
    m_Pages.push_back(Page);
    m_Book->AddPage(Page, Page->GetDisplayName());
  }

public:
  PreferenceDialog()
  : wxDialog(),
    m_Book(nullptr)
  {
    auto const ResTraceViewer = seec::Resource("TraceViewer",
                                               icu::Locale::getDefault());

    auto const ResText = ResTraceViewer["GUIText"]["PreferenceDialog"];
    auto const Title = seec::towxString(ResText["Title"].asStringOrDefault(""));

    if (!wxDialog::Create(nullptr, wxID_ANY, Title))
      return;

    // Create book to hold the individual preference frames.
    m_Book = new wxListbook(this, wxID_ANY);

    // Create individual pages of the book.
    AddPage(new LocaleSettingsWindow(this));

    // Create accept/cancel buttons.
    auto const Buttons = wxDialog::CreateStdDialogButtonSizer(wxOK | wxCANCEL);

    // Vertical sizer to hold each row of input.
    auto const ParentSizer = new wxBoxSizer(wxVERTICAL);

    int const BorderDir = wxLEFT | wxRIGHT;
    int const BorderSize = 5;
    int const InterSettingSpace = 10;

    ParentSizer->Add(m_Book, wxSizerFlags().Proportion(1)
                                           .Expand()
                                           .Border(BorderDir | wxTOP,
                                                   BorderSize));

    ParentSizer->AddSpacer(InterSettingSpace);

    ParentSizer->Add(Buttons, wxSizerFlags().Expand()
                                            .Border(BorderDir | wxBOTTOM,
                                                    BorderSize));

    SetSizerAndFit(ParentSizer);
  }

  virtual ~PreferenceDialog() = default;

  bool SaveValues()
  {
    for (auto const Page : m_Pages) {
      if (!Page->SaveValues()) {
        // TODO: Ensure page is selected.
        return false;
      }
    }

    return true;
  }
};

void showPreferenceDialog()
{
  PreferenceDialog Dlg;

  while (true) {
    auto const Result = Dlg.ShowModal();

    if (Result != wxID_OK || Dlg.SaveValues())
      break;
  }
}
