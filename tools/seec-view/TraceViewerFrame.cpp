//===- tools/seec-trace-view/TraceViewerFrame.cpp -------------------------===//
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

#include "seec/Clang/MappedProcessState.hpp"
#include "seec/Clang/MappedProcessTrace.hpp"
#include "seec/Clang/MappedThreadState.hpp"
#include "seec/Clang/PrintOnlinePythonTutorTrace.hpp"
#include "seec/ICU/Format.hpp"
#include "seec/ICU/Resources.hpp"
#include "seec/Trace/TraceReader.hpp"
#include "seec/Util/MakeFunction.hpp"
#include "seec/wxWidgets/StringConversion.hpp"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Path.h"

#include <wx/archive.h>
#include <wx/checkbox.h>
#include <wx/config.h>
#include <wx/file.h>
#include <wx/filedlg.h>
#include <wx/wfstream.h>
#include <wx/zipstrm.h>
#include <wx/aui/aui.h>
#include <wx/aui/framemanager.h>

#include <chrono>
#include <cinttypes>
#include <iostream>

#include "ActionRecord.hpp"
#include "ActionReplay.hpp"
#include "AnnotationEditor.hpp"
#include "CommonMenus.hpp"
#include "ExplanationViewer.hpp"
#include "LocaleSettings.hpp"
#include "NotifyContext.hpp"
#include "OpenTrace.hpp"
#include "ProcessMoveEvent.hpp"
#include "ProcessTimeGauge.hpp"
#include "SourceViewer.hpp"
#include "StateAccessToken.hpp"
#include "StateEvaluationTree.hpp"
#include "StateGraphViewer.hpp"
#include "StreamStatePanel.hpp"
#include "ThreadMoveEvent.hpp"
#include "ThreadTimeControl.hpp"
#include "TraceViewerApp.hpp"
#include "TraceViewerFrame.hpp"


char const * const cConfigKeyForPerspective = "/TraceViewerFrame/Perspective";
char const * const cConfigKeyForViewVersion = "/TraceViewerFrame/ViewVersion";
char const * const cConfigKeyForWidth       = "/TraceViewerFrame/Width";
char const * const cConfigKeyForHeight      = "/TraceViewerFrame/Height";

constexpr int32_t getViewVersion() { return 1; }


void TraceViewerFrame::createViewButton(wxMenu &Menu,
                                        wxWindow *Window,
                                        seec::Resource const &Table,
                                        char const * const Key)
{
  // This particular panel was not created for this trace viewer.
  if (!Window)
    return;

  auto &PaneInfo = Manager->GetPane(Window);
  if (!PaneInfo.IsOk())
    return;

  auto const Item =
    Menu.AppendCheckItem(wxID_ANY, seec::towxString(Table[Key].asString()));

  if (!Item)
    return;

  Menu.Bind(
    wxEVT_MENU,
    std::function<void (wxEvent &)>{
      [this, Window, Item] (wxEvent &) -> void {
        auto &PI = Manager->GetPane(Window);
        if (PI.IsOk()) {
          if (Item->IsChecked())
            PI.Show();
          else
            PI.Hide();
          Manager->Update();
        }
      }
    },
    Item->GetId());

  auto &PI = Manager->GetPane(Window);
  if (PI.IsOk() && PI.IsShown())
    Item->Check(true);

  ViewMenuLookup.insert(std::make_pair(Window, Item));
}

std::pair<std::unique_ptr<wxMenu>, wxString> TraceViewerFrame::createViewMenu()
{
  auto const Text =
    seec::Resource("TraceViewer", getLocale())["GUIText"]["MenuView"];

  if (U_FAILURE(Text.status()))
    return std::make_pair(nullptr, wxEmptyString);
  
  auto Menu = llvm::make_unique<wxMenu>();
  
  createViewButton(*Menu, ExplanationCtrl, Text, "Explanation");
  createViewButton(*Menu, GraphViewer,     Text, "Graph");
  createViewButton(*Menu, EvaluationTree,  Text, "EvaluationTree");
  createViewButton(*Menu, StreamState,     Text, "StreamState");
  
  return std::make_pair(std::move(Menu),
                        seec::towxString(Text["Title"].asString()));
}

std::pair<std::unique_ptr<wxMenu>, wxString> TraceViewerFrame::createToolsMenu()
{
  auto const Text = seec::Resource("TraceViewer")["GUIText"]["MenuTools"];

  if (U_FAILURE(Text.status()))
    return std::make_pair(nullptr, wxEmptyString);

  auto Menu = llvm::make_unique<wxMenu>();

  BindMenuItem(
    Menu->Append(wxID_ANY,
                 seec::towxString(Text["SaveDETBMP"])),
    [this, Text] (wxEvent &) {
      wxFileDialog Dlg(this,
                       seec::towxString(Text["SaveBMP"]),
                       "", "",
                       seec::towxString(Text["BMPFiles"]),
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      if (Dlg.ShowModal() == wxID_CANCEL)
        return;
      EvaluationTree->renderToBMP(Dlg.GetPath());
    });

  BindMenuItem(
    Menu->Append(wxID_ANY,
                 seec::towxString(Text["ExportGraphSVG"])),
    [this, Text] (wxEvent &) {
      wxFileDialog Dlg(this,
                       seec::towxString(Text["SaveGraphSVG"]),
                       "", "",
                       seec::towxString(Text["SVGFiles"]),
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
      if (Dlg.ShowModal() == wxID_CANCEL)
        return;
      GraphViewer->renderToSVG(Dlg.GetPath());
    });

  BindMenuItem(
  Menu->Append(wxID_ANY,
               seec::towxString(Text["ExportTraceOPT"])),
  [this, Text] (wxEvent &) {
    wxFileDialog Dlg(this,
                     seec::towxString(Text["SaveTraceOPT"]),
                     "", "",
                     /* file extensions */ "",
                     wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
    if (Dlg.ShowModal() == wxID_CANCEL)
      return;

    std::error_code FErr{};
    llvm::raw_fd_ostream FOut{Dlg.GetPath().ToStdString(), FErr,
                              llvm::sys::fs::OpenFlags::F_Text};
    if (FErr) {
      return;
    }
    
    seec::cm::PrintOnlinePythonTutor(Trace->getTrace(),
      seec::cm::OPTSettings{wxGetApp().getAugmentations()}
        .setVariableName("trace"),
      FOut);
  });

  return std::make_pair(std::move(Menu),
                        seec::towxString(Text["Title"].asString()));
}

void TraceViewerFrame::showState(size_t const ThreadID,
                                 type_safe::boolean const Initial)
{
  // Call SourceViewer's show() last, as it may produce highlight event
  // notifications that the other controls react to.
  auto const &ThreadState = State->getThread(ThreadID);
  m_ProcessTimeGauge->show(StateAccess, *State, ThreadState, ThreadID);
  ThreadTime->show(StateAccess, *State, ThreadState, ThreadID);
  ExplanationCtrl->show(StateAccess, *State, ThreadState);
  EvaluationTree->show(StateAccess, *State, ThreadState);
  
  // For the initial state, don't attempt to show anything in the graph viewer
  // - the underlying wxWebView most likely isn't ready to run any scripts.
  if (!Initial) {
    GraphViewer->show(StateAccess, *State, ThreadState);
  }
  
  SourceViewer->show(StateAccess, *State, ThreadState);
  StreamState->show(StateAccess, *State, ThreadState);
}

TraceViewerFrame::TraceViewerFrame()
: Trace(),
  State(),
  StateAccess(),
  Notifier(),
  Manager(),
  m_ProcessTimeGauge(nullptr),
  SourceViewer(nullptr),
  ExplanationCtrl(nullptr),
  GraphViewer(nullptr),
  EvaluationTree(nullptr),
  StreamState(nullptr),
  RecordingControl(nullptr),
  Recording(nullptr),
  Replay(nullptr),
  ViewMenuLookup(),
  ThreadTime(nullptr)
{}

TraceViewerFrame::TraceViewerFrame(wxWindow *Parent,
                                   std::unique_ptr<OpenTrace> TracePtr,
                                   wxWindowID ID,
                                   wxString const &Title,
                                   wxPoint const &Position,
                                   wxSize const &Size)
: TraceViewerFrame()
{
  Create(Parent, std::move(TracePtr), ID, Title, Position, Size);
}

TraceViewerFrame::~TraceViewerFrame() {
  // Finalize the recording. This stores the trace and recording into a combined
  // archive, and sets the archive up for automatic submission to a server.
#if defined(SEEC_USER_ACTION_RECORDING)
  Recording->finalize();
#endif

  auto const Config = wxConfig::Get();

  // Save the size of the frame.
  auto const WindowSize = GetSize();
  Config->Write(cConfigKeyForWidth, WindowSize.GetWidth());
  Config->Write(cConfigKeyForHeight, WindowSize.GetHeight());

  // Save the user's perspective.
  auto const Perspective = Manager->SavePerspective();
  Config->Write(cConfigKeyForPerspective, Perspective);
  Config->Write(cConfigKeyForViewVersion, getViewVersion());

  Config->Flush();

  // Notify the TraceViewerApp that we have been destroyed.
  auto &App = wxGetApp();
  App.removeTopLevelWindow(this);
}

bool TraceViewerFrame::Create(wxWindow *Parent,
                              std::unique_ptr<OpenTrace> TracePtr,
                              wxWindowID ID,
                              wxString const &Title,
                              wxPoint const &Position,
                              wxSize const &GivenSize)
{
  // Use the size of the user's last TraceViewerFrame, if available.
  auto const Config = wxConfig::Get();
  auto const Size = wxSize(
    Config->ReadLong(cConfigKeyForWidth,  GivenSize.GetWidth()),
    Config->ReadLong(cConfigKeyForHeight, GivenSize.GetHeight()));

  if (!wxFrame::Create(Parent, ID, Title, Position, Size))
    return false;
  
  // Set the trace.
  Trace = std::move(TracePtr);
  
  // Create a new state at the beginning of the trace.
  State = llvm::make_unique<seec::cm::ProcessState>(Trace->getTrace());
  
  // Create a new accessor token for this state.
  StateAccess = std::make_shared<StateAccessToken>();
  
  // Setup the action record.
  Recording = llvm::make_unique<ActionRecord>(Trace->getTrace());
  Recording->enable();
  
  // Setup the action replay frame.
  Replay = new ActionReplayFrame(this, Trace->getTrace());
  if (Trace->getRecording()) {
    Replay->LoadRecording(*(Trace->getRecording()));
    Recording->disable();
  }
  
  // Setup the context notifier.
  Notifier = llvm::make_unique<ContextNotifier>();

  // Get the GUIText from the TraceViewer ICU resources.
  auto const ResViewer = seec::Resource("TraceViewer", getLocale());
  auto const ResText = ResViewer["GUIText"];
  assert(U_SUCCESS(ResText.status()));
  
  // Setup the layout manager.
  Manager = seec::wxAuiManagerHandle(new wxAuiManager(this));

  if (State->getThreadCount() == 1) {
    // Setup the view for a single-threaded trace.
    
    // Create the process-time gauge.
    m_ProcessTimeGauge = new ProcessTimeGauge(this);
    Manager->AddPane(m_ProcessTimeGauge,
                     wxAuiPaneInfo().Name("ProcessTimeGauge")
                                    .CaptionVisible(false)
                                    .CloseButton(false)
                                    .DockFixed()
                                    .Layer(1)
                                    .Movable(false)
                                    .Top());

    // Create the thread time movement control.
    ThreadTime = new ThreadTimeControl(this, *Recording, Replay);
    auto const ThreadTimeTitle =
      seec::towxString(ResText["ScrollThreadTime"]["Title"].asString());
    Manager->AddPane(ThreadTime,
                     wxAuiPaneInfo{}.Name("ThreadTime")
                                    .Caption(ThreadTimeTitle)
                                    .Top()
                                    .ToolbarPane());

    // Create the action recording control.
#if defined(SEEC_USER_ACTION_RECORDING)
    constexpr bool ActionRecording = true;
#else
    constexpr bool ActionRecording = false;
#endif

    if (ActionRecording) {
      RecordingControl = new ActionRecordingControl(this, *Recording);
      auto const RecordingControlTitle =
        seec::towxString(ResViewer["RecordingToolbar"]["Title"].asString());
      Manager->AddPane(RecordingControl,
                       wxAuiPaneInfo().Name("RecordingControl")
                                      .Caption(RecordingControlTitle)
                                      .Top()
                                      .ToolbarPane());
    }

    // Create the source code viewer.
    SourceViewer = new SourceViewerPanel(this,
                                         *Trace,
                                         *Notifier,
                                         *Recording,
                                         *Replay,
                                         wxID_ANY,
                                         wxDefaultPosition,
                                         wxSize(200, 200));
    auto const SourceViewerTitle =
      seec::towxString(ResText["SourceBook_Title"].asString());
    Manager->AddPane(SourceViewer,
                     wxAuiPaneInfo{}.Name("SourceViewer")
                                    .Caption(SourceViewerTitle)
                                    .CentrePane());

    // Setup the explanation viewer.
    ExplanationCtrl = new ExplanationViewer(this,
                                            *Trace,
                                            *Notifier,
                                            *Recording,
                                            *Replay,
                                            wxID_ANY,
                                            wxDefaultPosition,
                                            wxSize(100, 100));
    auto const ExplanationCtrlTitle =
      seec::towxString(ResText["Explanation"]["Title"].asString());
    Manager->AddPane(ExplanationCtrl,
                     wxAuiPaneInfo{}.Name("ExplanationCtrl")
                                    .Caption(ExplanationCtrlTitle)
                                    .Bottom());

    // Create the stream viewer.
    StreamState = new StreamStatePanel(this,
                                       *Notifier,
                                       *Recording,
                                       *Replay,
                                       wxID_ANY,
                                       wxDefaultPosition,
                                       wxSize(100, 100));
    auto const StreamStateTitle =
      seec::towxString(ResText["StreamState"]["Title"].asString());
    Manager->AddPane(StreamState,
                     wxAuiPaneInfo{}.Name("StreamState")
                                    .Caption(StreamStateTitle)
                                    .Bottom()
                                    .MaximizeButton(true));

    // Create the evaluation tree.
    EvaluationTree = new StateEvaluationTreePanel(this,
                                                  *Trace,
                                                  *Notifier,
                                                  *Recording,
                                                  *Replay,
                                                  wxID_ANY,
                                                  wxDefaultPosition,
                                                  wxSize(100, 100));
    auto const EvaluationTreeTitle =
      seec::towxString(ResText["EvaluationTree"]["Title"].asString());
    Manager->AddPane(EvaluationTree,
                     wxAuiPaneInfo{}.Name("EvaluationTree")
                                    .Caption(EvaluationTreeTitle)
                                    .Right()
                                    .MaximizeButton(true));

    // Create the graph viewer.
    GraphViewer = new StateGraphViewerPanel(this,
                                            *Notifier,
                                            *Recording,
                                            *Replay,
                                            wxID_ANY,
                                            wxDefaultPosition,
                                            wxSize(200, 200));
    auto const GraphViewerTitle =
      seec::towxString(ResText["Graph"]["Title"].asString());
    Manager->AddPane(GraphViewer,
                     wxAuiPaneInfo{}.Name("GraphViewer")
                                    .Caption(GraphViewerTitle)
                                    .Right()
                                    .MaximizeButton(true));

    showState(/* thread */ 0, /* initial */ true);
  }
  else {
    // TODO: Setup the view for a multi-threaded trace.
  }

  // Catch the wxAuiManager's close event to update the view menu.
  Manager->Bind(wxEVT_AUI_PANE_CLOSE,
    std::function<void (wxAuiManagerEvent &)>{
      [this] (wxAuiManagerEvent &Ev) -> void {
        if (auto const PI = Ev.GetPane()) {
          auto const It = ViewMenuLookup.find(PI->window);
          if (It != ViewMenuLookup.end())
            It->second->Check(false);
        }
      }});

  // Load the user's last-used perspective.
  int32_t ViewVersion = 0;
  if (Config->Read(cConfigKeyForViewVersion, &ViewVersion)
      && ViewVersion == getViewVersion())
  {
    wxString Perspective;
    if (Config->Read(cConfigKeyForPerspective, &Perspective)) {
      Manager->LoadPerspective(Perspective, false);
    }
  }

  // Ensure that the unclosable frames are shown.
  if (RecordingControl)
    Manager->GetPane(RecordingControl).Show();
  if (ThreadTime)
    Manager->GetPane(ThreadTime).Show();
  if (m_ProcessTimeGauge)
    Manager->GetPane(m_ProcessTimeGauge).Show();
  Manager->GetPane(SourceViewer).Show();

  Manager->Update();

  // Setup the menus.
  auto menuBar = new wxMenuBar();
  append(menuBar, createFileMenu({wxID_SAVEAS}));
  append(menuBar, createEditMenu(*this));
  append(menuBar, createViewMenu());
  append(menuBar, createToolsMenu());
  append(menuBar, createRecordingMenu(*this));
  
  // TODO: Setup menu to open/close individual panels.
  
  SetMenuBar(menuBar);
  
  // Setup the event handling.
  Bind(wxEVT_COMMAND_MENU_SELECTED,
       &TraceViewerFrame::OnClose, this,
       wxID_CLOSE);

  Bind(wxEVT_COMMAND_MENU_SELECTED, &TraceViewerFrame::OnSaveAs, this,
       wxID_SAVEAS);
  
  Bind(SEEC_EV_PROCESS_MOVE, &TraceViewerFrame::OnProcessMove, this);
  
  Bind(SEEC_EV_THREAD_MOVE, &TraceViewerFrame::OnThreadMove, this);
  
  // Setup action recording.
  Bind(wxEVT_SIZE, std::function<void (wxSizeEvent &)> {
    [this] (wxSizeEvent &Ev) -> void {
      // TODO: Don't record if we're replaying.
      if (Recording) {
        auto const &Size = Ev.GetSize();
        Recording->recordEventL("TraceViewerFrame.Resize",
                                make_attribute("width", Size.GetWidth()),
                                make_attribute("height", Size.GetHeight()));
      }
      
      Ev.Skip();
    }
  });
  
  Replay->RegisterHandler("TraceViewerFrame.Resize",
                          {{"width", "height"}}, seec::make_function(
    [this] (int width, int height) -> void {
      this->SetSize(width, height);
      this->Layout();
    }));

  // Setup replay of contextual navigation.
  registerNavigationReplay(*this, StateAccess, *Replay);

  return true;
}

void TraceViewerFrame::OnClose(wxCommandEvent &Event) {
  Close(true);
}

class SaveExtraControlWindow final : public wxWindow
{
  wxCheckBox *m_IncludeAnnotations;

  wxCheckBox *m_IncludeActionRecording;

public:
  SaveExtraControlWindow(wxWindow * const Parent);

  bool getIncludeAnnotations() const {
    return m_IncludeAnnotations->GetValue();
  }

  bool getIncludeActionRecording() const {
    return m_IncludeActionRecording->GetValue();
  }
};

SaveExtraControlWindow::SaveExtraControlWindow(wxWindow * const Parent)
{
  if (!wxWindow::Create(Parent, wxID_ANY))
    return;

  auto const Res = seec::Resource("TraceViewer")["GUIText"]["SaveTrace"];

  auto ParentSizer = new wxBoxSizer(wxVERTICAL);

  m_IncludeAnnotations =
    new wxCheckBox(this, wxID_ANY,
                   seec::towxString(Res["IncludeAnnotations"]));

  m_IncludeAnnotations->SetValue(true);

  m_IncludeActionRecording =
    new wxCheckBox(this, wxID_ANY,
                   seec::towxString(Res["IncludeActionRecording"]));

  ParentSizer->Add(m_IncludeAnnotations, wxSizerFlags());
  ParentSizer->Add(m_IncludeActionRecording, wxSizerFlags());

  SetSizerAndFit(ParentSizer);
}

wxWindow *SaveControlCreator(wxWindow * const Parent)
{
  return new SaveExtraControlWindow(Parent);
}

void TraceViewerFrame::OnSaveAs(wxCommandEvent &Event)
{
  auto const Res = seec::Resource("TraceViewer")["GUIText"]["SaveTrace"];

  wxFileDialog SaveDlg(this,
                       seec::towxString(Res["Title"]),
                       /* default dir  */ wxEmptyString,
                       /* default file */ wxEmptyString,
                       seec::towxString(Res["FileType"]),
                       wxFD_SAVE | wxFD_OVERWRITE_PROMPT);

  SaveDlg.SetExtraControlCreator(SaveControlCreator);

  if (SaveDlg.ShowModal() == wxID_CANCEL)
    return;

  // Now to save!
  auto const ExtraControls = static_cast<SaveExtraControlWindow const *>
                                        (SaveDlg.GetExtraControl());

  // Create temporary archive stream.
  wxTempFileOutputStream Output(SaveDlg.GetPath());
  wxZipOutputStream ZipOutput(Output);
  if (!ZipOutput.IsOk()) {
    wxMessageDialog Dlg(this,
                        towxString(Res["OpenFailTitle"]),
                        towxString(Res["OpenFailMessage"]));
    Dlg.ShowModal();
    return;
  }

  // Write the trace.
  if (!Trace->getTrace().getUnmappedTrace()->writeToArchive(ZipOutput)) {
    wxMessageDialog Dlg(this,
                        towxString(Res["WriteTraceFailTitle"]),
                        towxString(Res["WriteTraceFailMessage"]));
    Dlg.ShowModal();
    return;
  }

  // Optionally write the annotations.
  if (ExtraControls->getIncludeAnnotations()) {
    if (!Trace->getAnnotations().writeToArchive(ZipOutput)) {
      wxMessageDialog Dlg(this,
                          towxString(Res["WriteAnnotationsFailTitle"]),
                          towxString(Res["WriteAnnotationsFailMessage"]));
      Dlg.ShowModal();
      return;
    }
  }

  // Optionally write the action recording.
  if (ExtraControls->getIncludeActionRecording()) {
    if (!Recording->writeToArchive(ZipOutput)) {
      wxMessageDialog Dlg(this,
                          towxString(Res["WriteActionRecordingFailTitle"]),
                          towxString(Res["WriteActionRecordingFailMessage"]));
      Dlg.ShowModal();
      return;
    }
  }

  // Commit the archive.
  if (!ZipOutput.Close()) {
    wxMessageDialog Dlg(this,
                        towxString(Res["ZipCloseFailTitle"]),
                        towxString(Res["ZipCloseFailMessage"]));
    Dlg.ShowModal();
    return;
  }

  if (!Output.Commit()) {
    wxMessageDialog Dlg(this,
                        towxString(Res["CommitFailTitle"]),
                        towxString(Res["CommitFailMessage"]));
    Dlg.ShowModal();
    Output.Discard();
    return;
  }
}

void TraceViewerFrame::OnProcessMove(ProcessMoveEvent &Event) {
  // Clear the current state from this graph viewer.
  EvaluationTree->clear();
  GraphViewer->clear();
  
  // Deny future access to the state (this will wait for current readers to
  // complete their work).
  if (StateAccess)
    StateAccess->invalidate();
  
  // Move the process.
  Event.getMover()(*State);
  
  // Create a new access token for the state.
  StateAccess = std::make_shared<StateAccessToken>();
  
  // Display the new state.
  if (State->getThreadCount() == 1) {
    showState(0);
  }
  else {
    // TODO: Show the state for a multi-threaded trace.
  }
}

void TraceViewerFrame::OnThreadMove(ThreadMoveEvent &Event) {
  // Clear the current state from this graph viewer.
  EvaluationTree->clear();
  GraphViewer->clear();
  
  // Deny future access to the state (this will wait for current readers to
  // complete their work).
  if (StateAccess)
    StateAccess->invalidate();
  
  // Move the thread.
  auto const Index = Event.getThreadIndex();
  auto &Thread = State->getThread(Index);
  
  Event.getMover()(Thread);
  
  // Create a new access token for the state.
  StateAccess = std::make_shared<StateAccessToken>();
  
  // Display the new state.
  showState(Index);
}

void TraceViewerFrame::editThreadTimeAnnotation()
{
  if (StateAccess) {
    if (auto Access = StateAccess->getAccess()) {
      auto const &ThreadState = State->getThread(0);
      showAnnotationEditorDialog(this, *Trace, ThreadState);
    }
  }
}
