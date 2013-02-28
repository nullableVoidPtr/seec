//===- tools/seec-trace-view/TraceViewerFrame.hpp -------------------------===//
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

#ifndef SEEC_TRACE_VIEW_TRACEVIEWERFRAME_HPP
#define SEEC_TRACE_VIEW_TRACEVIEWERFRAME_HPP

#include "seec/Clang/MappedValue.hpp"
#include "seec/Trace/ProcessState.hpp"
#include "seec/Trace/TraceReader.hpp"

#include <wx/wx.h>
#include <wx/stdpaths.h>
#include <wx/aui/aui.h>
#include <wx/aui/auibook.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include <memory>

#include "OpenTrace.hpp"
#include "ProcessTimeControl.hpp"
#include "SourceViewer.hpp"
#include "StateViewer.hpp"
#include "ThreadTimeControl.hpp"

class TraceViewerFrame : public wxFrame
{
  /// Stores information about the currently open trace.
  std::unique_ptr<OpenTrace> Trace;

  /// Stores the current ProcessState.
  std::unique_ptr<seec::trace::ProcessState> State;

  /// Stores SeeC-Clang mapped Values.
  std::shared_ptr<seec::cm::ValueStore const> ValueStore;

  /// Shows source code.
  SourceViewerPanel *SourceViewer;

  /// Shows the current state.
  StateViewerPanel *StateViewer;
  
  
  /// \name Multi-threaded traces
  /// @{
  
  /// Controls the process time (in multi-threaded traces).
  
  /// @} (Multi-threaded traces)
  
  
  /// \name Single-threaded traces
  /// @{
  
  /// Controls the thread time (in single-threaded traces).
  ThreadTimeControl *ThreadTime;
  
  /// @} (Single-threaded traces)

public:
  TraceViewerFrame()
  : Trace(),
    State(),
    ValueStore(),
    SourceViewer(nullptr),
    StateViewer(nullptr)
  {}

  TraceViewerFrame(wxWindow *Parent,
                   std::unique_ptr<OpenTrace> &&TracePtr,
                   wxWindowID ID = wxID_ANY,
                   wxString const &Title = wxString(),
                   wxPoint const &Position = wxDefaultPosition,
                   wxSize const &Size = wxDefaultSize)
  : Trace(),
    State(),
    ValueStore(),
    SourceViewer(nullptr),
    StateViewer(nullptr)
  {
    Create(Parent, std::move(TracePtr), ID, Title, Position, Size);
  }

  /// \brief Destructor.
  ~TraceViewerFrame();

  bool Create(wxWindow *Parent,
              std::unique_ptr<OpenTrace> &&TracePtr,
              wxWindowID ID = wxID_ANY,
              wxString const &Title = wxString(),
              wxPoint const &Position = wxDefaultPosition,
              wxSize const &Size = wxDefaultSize);

  /// \brief Close the current file.
  void OnClose(wxCommandEvent &Event);

  void OnProcessTimeChanged(ProcessTimeEvent &Event);

  void OnThreadTimeChanged(ThreadTimeEvent &Event);

private:
  DECLARE_EVENT_TABLE()
};

#endif // SEEC_TRACE_VIEW_TRACEVIEWERFRAME_HPP
