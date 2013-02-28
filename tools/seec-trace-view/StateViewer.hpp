//===- tools/seec-trace-view/StateViewer.hpp ------------------------------===//
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

#ifndef SEEC_TRACE_VIEW_STATEVIEWER_HPP
#define SEEC_TRACE_VIEW_STATEVIEWER_HPP

#include "seec/Clang/MappedValue.hpp"
#include "seec/Trace/ProcessState.hpp"

#include <wx/wx.h>
#include <wx/panel.h>
#include "seec/wxWidgets/CleanPreprocessor.h"

#include <vector>

class OpenTrace;
class ThreadStateViewerPanel;
class MallocViewerPanel;
class wxAuiNotebook;
class wxDataViewCtrl;

///
class StateViewerPanel : public wxPanel
{
  /// \name Thread-specific state.
  /// @{
  
  wxAuiNotebook *ThreadBook;
  
  std::vector<ThreadStateViewerPanel *> ThreadViewers;
  
  /// @} (Thread-specific state)
  
  
  /// \name Process-wide state.
  /// @{
  
  wxAuiNotebook *StateBook;
  
  MallocViewerPanel *MallocViewer;
  
  /// @} (Process-wide state)
  
  
  /// The associated trace information.
  OpenTrace const *Trace;

public:
  StateViewerPanel()
  : wxPanel(),
    ThreadBook(),
    ThreadViewers(),
    StateBook(nullptr),
    MallocViewer(nullptr),
    Trace(nullptr)
  {}

  StateViewerPanel(wxWindow *Parent,
                   OpenTrace const &TheTrace,
                   wxWindowID ID = wxID_ANY,
                   wxPoint const &Position = wxDefaultPosition,
                   wxSize const &Size = wxDefaultSize)
  : wxPanel(),
    ThreadBook(),
    ThreadViewers(),
    StateBook(nullptr),
    MallocViewer(nullptr),
    Trace(nullptr)
  {
    Create(Parent, TheTrace, ID, Position, Size);
  }

  ~StateViewerPanel();

  bool Create(wxWindow *Parent,
              OpenTrace const &TheTrace,
              wxWindowID ID = wxID_ANY,
              wxPoint const &Position = wxDefaultPosition,
              wxSize const &Size = wxDefaultSize);

  void show(seec::trace::ProcessState &State,
            std::shared_ptr<seec::cm::ValueStore const> ValueStore);

  void clear();
};

#endif // SEEC_TRACE_VIEW_STATEVIEWER_HPP
