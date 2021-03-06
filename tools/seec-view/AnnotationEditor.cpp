//===- tools/seec-trace-view/AnnotationEditor.cpp -------------------------===//
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

#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/listbook.h>
#include <wx/stc/stc.h>

#include "AnnotationEditor.hpp"
#include "ColourSchemeSettings.hpp"
#include "OpenTrace.hpp"
#include "SourceViewerSettings.hpp"
#include "TraceViewerApp.hpp"

class AnnotationEditorDialog final : public wxDialog
{
  AnnotationPoint m_Point;

  wxStyledTextCtrl *m_Text;

  wxCheckBox *m_SuppressEPV;

  void OnButton(wxCommandEvent &Ev);

public:
  /// \brief Constructor.
  ///
  AnnotationEditorDialog(wxWindow *Parent, AnnotationPoint ForPoint);

  /// \brief Destructor.
  ///
  virtual ~AnnotationEditorDialog() override = default;
};

void AnnotationEditorDialog::OnButton(wxCommandEvent &Ev)
{
  if (Ev.GetId() != wxID_OK) {
    Ev.Skip();
    return;
  }

  m_Point.setText(m_Text->GetValue());

  if (m_SuppressEPV)
    m_Point.setSuppressEPV(m_SuppressEPV->GetValue());

  Ev.Skip();
}

AnnotationEditorDialog::AnnotationEditorDialog(wxWindow *Parent,
                                               AnnotationPoint ForPoint)
: m_Point(std::move(ForPoint)),
  m_Text(nullptr),
  m_SuppressEPV(nullptr)
{
  auto const Res = seec::Resource("TraceViewer")["GUIText"]["AnnotationEditor"];
  auto const DlgStyle = wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER;

  if (!wxDialog::Create(Parent, wxID_ANY, seec::towxString(Res["EditorTitle"]),
                        wxDefaultPosition, wxSize(700, 300), DlgStyle))
  {
    return;
  }

  Bind(wxEVT_BUTTON, &AnnotationEditorDialog::OnButton, this);

  // Setup text editor.
  m_Text = new wxStyledTextCtrl(this, wxID_ANY);

  setupStylesFromColourScheme(*m_Text,
    *(wxGetApp().getColourSchemeSettings().getColourScheme()));

  m_Text->SetWrapMode(wxSTC_WRAP_WORD);
  m_Text->SetValue(m_Point.getText());

  // If this annotation is on an AST node, setup the checkbox for SuppressEPV.
  if (ForPoint.isForDecl() || ForPoint.isForStmt()) {
    m_SuppressEPV = new wxCheckBox(this, wxID_ANY,
                                   seec::towxString(Res["SuppressEPV"]));
    m_SuppressEPV->SetValue(ForPoint.hasSuppressEPV());
  }

  // Create accept/cancel buttons.
  auto const Buttons = wxDialog::CreateStdDialogButtonSizer(wxOK | wxCANCEL);

  // Vertical sizer to hold each row of input.
  auto const ParentSizer = new wxBoxSizer(wxVERTICAL);

  int const BorderDir = wxLEFT | wxRIGHT;
  int const BorderSize = 5;
  int const InterSettingSpace = 10;

  ParentSizer->Add(m_Text,
                   wxSizerFlags().Proportion(1)
                                 .Expand()
                                 .Border(BorderDir | wxTOP, BorderSize));

  ParentSizer->AddSpacer(InterSettingSpace);

  if (m_SuppressEPV) {
    ParentSizer->Add(m_SuppressEPV,
                     wxSizerFlags().Border(BorderDir, BorderSize));
    ParentSizer->AddSpacer(InterSettingSpace);
  }

  ParentSizer->Add(Buttons,
                   wxSizerFlags().Expand()
                                 .Border(BorderDir | wxBOTTOM, BorderSize));

  SetSizer(ParentSizer);
}

void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                clang::Decl const *Decl)
{
  auto &Annotations = Trace.getAnnotations();
  auto Point = Annotations.getOrCreatePointForNode(Trace.getTrace(), Decl);
  if (!Point.assigned<AnnotationPoint>())
    return;

  auto Dlg = new AnnotationEditorDialog(Parent, Point.move<AnnotationPoint>());
  if (Dlg)
    Dlg->Show();
}

void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                clang::Stmt const *Statement)
{
  auto &Annotations = Trace.getAnnotations();
  auto Point = Annotations.getOrCreatePointForNode(Trace.getTrace(), Statement);
  if (!Point.assigned<AnnotationPoint>())
    return;

  auto Dlg = new AnnotationEditorDialog(Parent, Point.move<AnnotationPoint>());
  if (Dlg)
    Dlg->Show();
}

void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                seec::cm::ThreadState const &State)
{
  auto &Annotations = Trace.getAnnotations();
  auto Point = Annotations.getOrCreatePointForThreadState(State);
  if (!Point.assigned<AnnotationPoint>())
    return;

  auto Dlg = new AnnotationEditorDialog(Parent, Point.move<AnnotationPoint>());
  if (Dlg)
    Dlg->Show();
}
