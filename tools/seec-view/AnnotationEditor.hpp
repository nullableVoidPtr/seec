//===- tools/seec-trace-view/AnnotationEditor.hpp -------------------------===//
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

#ifndef SEEC_TRACE_VIEW_ANNOTATIONEDITOR_HPP
#define SEEC_TRACE_VIEW_ANNOTATIONEDITOR_HPP

namespace clang {
  class Decl;
  class Stmt;
}

namespace seec {
  namespace cm {
    class ThreadState;
  }
}

class OpenTrace;
class wxWindow;

/// \brief Show a dialog allowing the user to edit a single \c AnnotationPoint.
///
void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                clang::Decl const *Declaration);

/// \brief Show a dialog allowing the user to edit a single \c AnnotationPoint.
///
void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                clang::Stmt const *Statement);

/// \brief Show a dialog allowing the user to edit a single \c AnnotationPoint.
///
void showAnnotationEditorDialog(wxWindow *Parent,
                                OpenTrace &Trace,
                                seec::cm::ThreadState const &State);

#endif // SEEC_TRACE_VIEW_ANNOTATIONEDITOR_HPP
