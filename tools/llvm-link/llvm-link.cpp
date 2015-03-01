//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/Linker/Linker.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include <memory>
using namespace llvm;

enum LibraryLinkage { Dynamic, Static };

static cl::list<LibraryLinkage>
LinkDynamicLibraries("B", cl::Prefix, cl::ZeroOrMore, cl::ValueRequired,
                     cl::desc("Control library linkage"),
                     cl::values(
                      clEnumValN(Dynamic, "dynamic", "Link against shared libraries"),
                      clEnumValN(Static,  "static",  "Do not link against shared libraries"),
                      clEnumValEnd));

static cl::list<std::string>
InputFilenames(cl::Positional, cl::OneOrMore,
               cl::desc("<input bitcode files>"));

static cl::list<std::string>
LibrarySearchPaths("L", cl::Prefix, cl::ZeroOrMore, 
                   cl::desc("Library search paths"),
                   cl::value_desc("dir"));

static cl::alias
LibrarySearchPathsA("-library-path", cl::desc("Alias for -L"),
                    cl::value_desc("dir"), cl::aliasopt(LibrarySearchPaths));

static cl::list<std::string>
Libraries("l", cl::Prefix, cl::ZeroOrMore,
          cl::desc("Libraries"), cl::value_desc("library"));

static cl::alias
LibrariesA("-library", cl::desc("Alias for -l"), cl::value_desc("library"),
           cl::aliasopt(Libraries));

static cl::opt<std::string>
OutputFilename("o", cl::desc("Override output filename"), cl::init("-"),
               cl::value_desc("filename"));

static cl::opt<bool>
Force("f", cl::desc("Enable binary output on terminals"));

static cl::opt<bool>
OutputAssembly("S",
         cl::desc("Write output as LLVM assembly"), cl::Hidden);

static cl::opt<bool>
Verbose("v", cl::desc("Print information about actions taken"));

static cl::opt<bool>
DumpAsm("d", cl::desc("Print assembly as linked"), cl::Hidden);

static cl::opt<bool>
SuppressWarnings("suppress-warnings", cl::desc("Suppress all linking warnings"),
                 cl::init(false));

// Read the specified bitcode file in and return it. This routine searches the
// link path for the specified file to try to find it...
//
static std::unique_ptr<Module>
loadFile(const char *argv0, const std::string &FN, LLVMContext &Context) {
  SMDiagnostic Err;
  if (Verbose) errs() << "Loading '" << FN << "'\n";
  std::unique_ptr<Module> Result = getLazyIRFileModule(FN, Err, Context);
  if (!Result)
    Err.print(argv0, errs());

  return Result;
}

static void diagnosticHandler(const DiagnosticInfo &DI) {
  unsigned Severity = DI.getSeverity();
  switch (Severity) {
  case DS_Error:
    errs() << "ERROR: ";
    break;
  case DS_Warning:
    if (SuppressWarnings)
      return;
    errs() << "WARNING: ";
    break;
  case DS_Remark:
  case DS_Note:
    llvm_unreachable("Only expecting warnings and errors");
  }

  DiagnosticPrinterRawOStream DP(errs());
  DI.print(DP);
  errs() << '\n';
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal();
  PrettyStackTraceProgram X(argc, argv);

  LLVMContext &Context = getGlobalContext();
  llvm_shutdown_obj Y;  // Call llvm_shutdown() on exit.
  cl::ParseCommandLineOptions(argc, argv, "llvm linker\n");

  auto Composite = make_unique<Module>("llvm-link", Context);
  Linker L(Composite.get(), diagnosticHandler);

  for (unsigned i = 0; i < InputFilenames.size(); ++i) {
    std::unique_ptr<Module> M = loadFile(argv[0], InputFilenames[i], Context);
    if (!M.get()) {
      errs() << argv[0] << ": error loading file '" <<InputFilenames[i]<< "'\n";
      return 1;
    }

    // Otherwise, FilePos or LibPos is smaller (or all are -1).
    if (FilePos < LibPos) {
      // Link in a module or archive
      const std::string &FileName = *FileIt++;
      if (!sys::fs::exists(FileName)) {
        errs() << argv[0] << ": invalid file name: '" << FileName << "'\n";
        return 1;
      }

      bool IsNative;
      if (isFileType(FileName, sys::fs::file_magic::archive)) {
        // Link the archive in if it will resolve symbols
        if (L.linkInArchive(FileName, IsNative))
        {
          errs() << argv[0] << ": error linking archive: '" << FileName << "'\n";
          return 1;
        }
      }
      else if (isFileType(FileName, sys::fs::file_magic::bitcode)) {
        // Link the bitcode file in
        if (L.linkInFile(FileName, IsNative)) {
          errs() << argv[0] << ": error linking bitcode file: '" << FileName << "'\n";
          return 1;
        }
      }
      else {
        // Not an archive nor bitcode so attempt to parse it as LLVM
        // assembly.
        SMDiagnostic Err;
        std::auto_ptr<Module> M(ParseIRFile(FileName, Err, Context));
        if (M.get() == 0) {
          errs() << argv[0] << ": error parsing LLVM assembly file: '" << FileName << "'\n";
          return 1;
        }
        std::string ErrMessage;
        if (L.linkInModule(M.get(), &ErrMessage)) {
          errs() << argv[0] << ": link error in '" << FileName
                 << "': " << ErrMessage << "\n";
          return 1;
        }
      }
      continue;
    }

    if (L.linkInModule(M.get()))
      return 1;
  }

  Module &Composite = *L.getModule();
  if (DumpAsm) errs() << "Here's the assembly:\n" << Composite;

  std::error_code EC;
  tool_output_file Out(OutputFilename, EC, sys::fs::F_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (verifyModule(Composite)) {
    errs() << argv[0] << ": linked module is broken!\n";
    return 1;
  }

  if (Verbose) errs() << "Writing bitcode...\n";
  if (OutputAssembly) {
    Out.os() << Composite;
  } else if (Force || !CheckBitcodeOutputToConsole(Out.os(), true))
    WriteBitcodeToFile(&Composite, Out.os());

  // Declare success.
  Out.keep();

  return 0;
}
