//===- tools/seec-trace-view/OpenTrace.cpp --------------------------------===//
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

#include "seec/ICU/LazyMessage.hpp"
#include "seec/Trace/TraceReader.hpp"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#include <wx/wx.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/wfstream.h>
#include <wx/xml/xml.h>
#include <wx/zipstrm.h>

#include "OpenTrace.hpp"

#include <memory>


OpenTrace::OpenTrace(std::string WithTempDir,
                     std::vector<std::string> WithTempFiles,
                     std::unique_ptr<seec::cm::ProcessTrace> WithTrace,
                     std::unique_ptr<wxXmlDocument> WithRecording,
                     AnnotationCollection WithAnnotations)
: TempDir(std::move(WithTempDir)),
  TempFiles(std::move(WithTempFiles)),
  Trace(std::move(WithTrace)),
  Recording(std::move(WithRecording)),
  Annotations(std::move(WithAnnotations))
{}

OpenTrace::OpenTrace(std::unique_ptr<seec::cm::ProcessTrace> WithTrace)
: OpenTrace(std::string{},
            std::vector<std::string>{},
            std::move(WithTrace),
            std::unique_ptr<wxXmlDocument>{},
            AnnotationCollection{})
{}

seec::Maybe<std::unique_ptr<seec::cm::ProcessTrace>, seec::Error>
OpenTrace::ReadTraceFromFilePath(wxString const &FilePath)
{
  using namespace seec;
  using namespace seec::trace;
  
  // Attempt to create an input allocator for the file.
  auto MaybeIBA = InputBufferAllocator::createFor(FilePath.ToStdString());
  if (MaybeIBA.assigned<Error>())
    return MaybeIBA.move<Error>();
  
  assert(MaybeIBA.assigned<seec::trace::InputBufferAllocator>());
  
  // Attempt to load the SeeC-Clang Mapped process trace.
  auto IBAPtrTemp =
    llvm::make_unique<seec::trace::InputBufferAllocator>
                     (MaybeIBA.move<seec::trace::InputBufferAllocator>());
  
  return seec::cm::ProcessTrace::load(std::move(IBAPtrTemp));
}

seec::Maybe<std::unique_ptr<OpenTrace>, seec::Error>
OpenTrace::FromRecordingArchive(wxString const &FilePath)
{
  // Attempt to open the file for reading.
  wxFFileInputStream RawInput{FilePath};
  if (!RawInput.IsOk())
    return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                        {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
  
  // Create a temporary directory to hold the extracted trace files.
  // TODO: Delete this directory if the rest of the process fails.
  auto const TempPath = wxFileName::CreateTempFileName("SeeC");
  wxRemoveFile(TempPath);
  if (!wxMkdir(TempPath)) {
    wxLogDebug("Error creating temporary directory.");
    return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                        {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
  }
  
  // Attempt to read from the file.
  wxZipInputStream Input{RawInput};
  std::unique_ptr<wxZipEntry> Entry;
  std::unique_ptr<wxXmlDocument> Record;
  AnnotationCollection Annotations;
  std::vector<std::string> TempFiles;
  wxString TracePath;
  
  while (Entry.reset(Input.GetNextEntry()), Entry) {
    // Skip dir entries, because file entries have the complete path.
    if (Entry->IsDir())
      continue;
    
    auto const &Name = Entry->GetName();
    wxFileName Path{Name};
    
    if (Name == "record.xml") {
      Record.reset(new wxXmlDocument(Input));
      if (!Record->IsOk()) {
        return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                            {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
      }
    }
    else if (Name == "annotations.xml") {
      auto XmlDoc = llvm::make_unique<wxXmlDocument>(Input);
      if (!XmlDoc->IsOk()) {
        return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                            {"GUIText", "OpenTrace_Error_AnnotationXml"})};
      }

      auto MaybeAnnotations = AnnotationCollection::fromDoc(std::move(XmlDoc));
      if (!MaybeAnnotations.assigned<AnnotationCollection>()) {
        return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                            {"GUIText", "OpenTrace_Error_AnnotationBad"})};
      }

      Annotations = MaybeAnnotations.move<AnnotationCollection>();
    }
    else if (Path.GetDirCount() == 1 && Path.GetDirs()[0] == "trace") {
      Path.RemoveDir(0);
      
      auto const FullPath = TempPath
                          + wxFileName::GetPathSeparator()
                          + Path.GetFullPath();
      
      wxFFileOutputStream Out{FullPath};
      if (!Out.IsOk()) {
        return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                            {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
      }
      
      Out.Write(Input);
      TempFiles.emplace_back(FullPath.ToStdString());
      TracePath = FullPath;
    }
    else {
      wxLogDebug("Unknown entry: '%s'", Name);
      return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
                            {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
    }
  }
  
  auto MaybeTrace = ReadTraceFromFilePath(TracePath);
  if (MaybeTrace.assigned<seec::Error>())
    return MaybeTrace.move<seec::Error>();
  
  return std::unique_ptr<OpenTrace>{
    new OpenTrace(TempPath.ToStdString(),
                  std::move(TempFiles),
                  MaybeTrace.move<std::unique_ptr<seec::cm::ProcessTrace>>(),
                  std::move(Record),
                  std::move(Annotations))};
}

OpenTrace::~OpenTrace()
{
  for (auto const &File : TempFiles)
    wxRemoveFile(File);
  
  if (!TempDir.empty())
    wxRmdir(TempDir);
}

seec::Maybe<std::unique_ptr<OpenTrace>, seec::Error>
OpenTrace::FromFilePath(wxString const &FilePath)
{
  wxFileName FileName{FilePath};
  
  if (FileName.FileExists()) {
    if (FilePath.EndsWith(".seec") &&
        seec::trace::doesLookLikeTraceFile(FilePath.c_str()))
    {
      auto MaybeTrace = ReadTraceFromFilePath(FilePath);
      if (MaybeTrace.assigned<seec::Error>())
        return MaybeTrace.move<seec::Error>();
      
      return std::unique_ptr<OpenTrace>{
        new OpenTrace(MaybeTrace.move<std::unique_ptr<seec::cm::ProcessTrace>>())};
    }
    else {
      return FromRecordingArchive(FilePath);
    }
  }
  
  return seec::Error{seec::LazyMessageByRef::create("TraceViewer",
    {"GUIText", "OpenTrace_Error_LoadProcessTrace"})};
}
