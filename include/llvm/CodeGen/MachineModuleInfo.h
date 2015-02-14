//===-- llvm/CodeGen/MachineModuleInfo.h ------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Collect meta information for a module.  This information should be in a
// neutral form that can be used by different debugging and exception handling
// schemes.
//
// The organization of information is primarily clustered around the source
// compile units.  The main exception is source line correspondence where
// inlining may interleave code from various compile units.
//
// The following information can be retrieved from the MachineModuleInfo.
//
//  -- Source directories - Directories are uniqued based on their canonical
//     string and assigned a sequential numeric ID (base 1.)
//  -- Source files - Files are also uniqued based on their name and directory
//     ID.  A file ID is sequential number (base 1.)
//  -- Source line correspondence - A vector of file ID, line#, column# triples.
//     A DEBUG_LOCATION instruction is generated  by the DAG Legalizer
//     corresponding to each entry in the source line list.  This allows a debug
//     emitter to generate labels referenced by debug information tables.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMODULEINFO_H
#define LLVM_CODEGEN_MACHINEMODULEINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/LibCallSemantics.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MachineLocation.h"
#include "llvm/Pass.h"
#include "llvm/Support/DataTypes.h"
#include "llvm/Support/Dwarf.h"

namespace llvm {

//===----------------------------------------------------------------------===//
// Forward declarations.
class Constant;
class GlobalVariable;
class MDNode;
class MMIAddrLabelMap;
class MachineBasicBlock;
class MachineFunction;
class Module;
class PointerType;
class StructType;
class TargetMachine;

//===----------------------------------------------------------------------===//
/// LandingPadInfo - This structure is used to retain landing pad info for
/// the current function.
///
struct LandingPadInfo {
  MachineBasicBlock *LandingPadBlock;    // Landing pad block.
  SmallVector<MCSymbol*, 1> BeginLabels; // Labels prior to invoke.
  SmallVector<MCSymbol*, 1> EndLabels;   // Labels after invoke.
  SmallVector<MCSymbol*, 1> ClauseLabels; // Labels for each clause.
  MCSymbol *LandingPadLabel;             // Label at beginning of landing pad.
  const Function *Personality;           // Personality function.
  std::vector<int> TypeIds;              // List of type ids (filters negative)

  explicit LandingPadInfo(MachineBasicBlock *MBB)
    : LandingPadBlock(MBB), LandingPadLabel(nullptr), Personality(nullptr) {}
};

//===----------------------------------------------------------------------===//
/// MachineModuleInfoImpl - This class can be derived from and used by targets
/// to hold private target-specific information for each Module.  Objects of
/// type are accessed/created with MMI::getInfo and destroyed when the
/// MachineModuleInfo is destroyed.
/// 
class MachineModuleInfoImpl {
public:
  typedef PointerIntPair<MCSymbol*, 1, bool> StubValueTy;
  virtual ~MachineModuleInfoImpl();
  typedef std::vector<std::pair<MCSymbol*, StubValueTy> > SymbolListTy;
protected:
  static SymbolListTy GetSortedStubs(const DenseMap<MCSymbol*, StubValueTy>&);
};

//===----------------------------------------------------------------------===//
/// MachineModuleInfo - This class contains meta information specific to a
/// module.  Queries can be made by different debugging and exception handling
/// schemes and reformated for specific use.
///
class MachineModuleInfo : public ImmutablePass {
  /// TM - The TargetMachine used for code generation.
  const TargetMachine *TM;

  /// Context - This is the MCContext used for the entire code generator.
  MCContext Context;

  /// TheModule - This is the LLVM Module being worked on.
  const Module *TheModule;

  /// ObjFileMMI - This is the object-file-format-specific implementation of
  /// MachineModuleInfoImpl, which lets targets accumulate whatever info they
  /// want.
  MachineModuleInfoImpl *ObjFileMMI;

  /// MachineFunctions - Cached machine functions of MachineFunctionAnalysis 
  /// passes.
  DenseMap<const Function*, MachineFunction*> MachineFunctions;

  /// List of moves done by a function's prolog.  Used to construct frame maps
  /// by debug and exception handling consumers.
  std::vector<MCCFIInstruction> FrameInstructions;

  /// LandingPads - List of LandingPadInfo describing the landing pad
  /// information in the current function.
  std::vector<LandingPadInfo> LandingPads;

  /// LPadToCallSiteMap - Map a landing pad's EH symbol to the call site
  /// indexes.
  DenseMap<MCSymbol*, SmallVector<unsigned, 4> > LPadToCallSiteMap;

  /// CallSiteMap - Map of invoke call site index values to associated begin
  /// EH_LABEL for the current function.
  DenseMap<MCSymbol*, unsigned> CallSiteMap;

  /// CurCallSite - The current call site index being processed, if any. 0 if
  /// none.
  unsigned CurCallSite;

  /// TypeInfos - List of C++ TypeInfo used in the current function.
  std::vector<const GlobalValue *> TypeInfos;

  /// FilterIds - List of typeids encoding filters used in the current function.
  std::vector<unsigned> FilterIds;

  /// FilterEnds - List of the indices in FilterIds corresponding to filter
  /// terminators.
  std::vector<unsigned> FilterEnds;

  /// Personalities - Vector of all personality functions ever seen. Used to
  /// emit common EH frames.
  std::vector<const Function *> Personalities;

  /// UsedFunctions - The functions in the @llvm.used list in a more easily
  /// searchable format.  This does not include the functions in
  /// llvm.compiler.used.
  SmallPtrSet<const Function *, 32> UsedFunctions;

  /// AddrLabelSymbols - This map keeps track of which symbol is being used for
  /// the specified basic block's address of label.
  MMIAddrLabelMap *AddrLabelSymbols;

  bool CallsEHReturn;
  bool CallsUnwindInit;

  /// DbgInfoAvailable - True if debugging information is available
  /// in this module.
  bool DbgInfoAvailable;

  /// UsesVAFloatArgument - True if this module calls VarArg function with
  /// floating-point arguments.  This is used to emit an undefined reference
  /// to _fltused on Windows targets.
  bool UsesVAFloatArgument;

  /// UsesMorestackAddr - True if the module calls the __morestack function
  /// indirectly, as is required under the large code model on x86. This is used
  /// to emit a definition of a symbol, __morestack_addr, containing the
  /// address. See comments in lib/Target/X86/X86FrameLowering.cpp for more
  /// details.
  bool UsesMorestackAddr;

  EHPersonality PersonalityTypeCache;

public:
  static char ID; // Pass identification, replacement for typeid

  struct VariableDbgInfo {
    TrackingMDNodeRef Var;
    TrackingMDNodeRef Expr;
    unsigned Slot;
    DebugLoc Loc;

    VariableDbgInfo(MDNode *Var, MDNode *Expr, unsigned Slot, DebugLoc Loc)
        : Var(Var), Expr(Expr), Slot(Slot), Loc(Loc) {}
  };
  typedef SmallVector<VariableDbgInfo, 4> VariableDbgInfoMapTy;
  VariableDbgInfoMapTy VariableDbgInfos;

  MachineModuleInfo();  // DUMMY CONSTRUCTOR, DO NOT CALL.
  // Real constructor.
  MachineModuleInfo(const TargetMachine &TM);
  ~MachineModuleInfo();

  // Initialization and Finalization
  bool doInitialization(Module &) override;
  bool doFinalization(Module &) override;

  /// EndFunction - Discard function meta information.
  ///
  void EndFunction();

  const TargetMachine& getTargetMachine() const { return *TM; }

  const MCContext &getContext() const { return Context; }
  MCContext &getContext() { return Context; }

  void setModule(const Module *M) { TheModule = M; }
  const Module *getModule() const { return TheModule; }

  /// getInfo - Keep track of various per-function pieces of information for
  /// backends that would like to do so.
  ///
  template<typename Ty>
  Ty &getObjFileInfo() {
    if (ObjFileMMI == nullptr)
      ObjFileMMI = new Ty(*this);
    return *static_cast<Ty*>(ObjFileMMI);
  }

  template<typename Ty>
  const Ty &getObjFileInfo() const {
    return const_cast<MachineModuleInfo*>(this)->getObjFileInfo<Ty>();
  }

  /// AnalyzeModule - Scan the module for global debug information.
  ///
  void AnalyzeModule(const Module &M);

  /// hasDebugInfo - Returns true if valid debug info is present.
  ///
  bool hasDebugInfo() const { return DbgInfoAvailable; }
  void setDebugInfoAvailability(bool avail) { DbgInfoAvailable = avail; }

  bool callsEHReturn() const { return CallsEHReturn; }
  void setCallsEHReturn(bool b) { CallsEHReturn = b; }

  bool callsUnwindInit() const { return CallsUnwindInit; }
  void setCallsUnwindInit(bool b) { CallsUnwindInit = b; }

  bool usesVAFloatArgument() const {
    return UsesVAFloatArgument;
  }

  void setUsesVAFloatArgument(bool b) {
    UsesVAFloatArgument = b;
  }

  bool usesMorestackAddr() const {
    return UsesMorestackAddr;
  }

  void setUsesMorestackAddr(bool b) {
    UsesMorestackAddr = b;
  }

  /// \brief Returns a reference to a list of cfi instructions in the current
  /// function's prologue.  Used to construct frame maps for debug and exception
  /// handling comsumers.
  const std::vector<MCCFIInstruction> &getFrameInstructions() const {
    return FrameInstructions;
  }

  unsigned LLVM_ATTRIBUTE_UNUSED_RESULT
  addFrameInst(const MCCFIInstruction &Inst) {
    FrameInstructions.push_back(Inst);
    return FrameInstructions.size() - 1;
  }

  /// getAddrLabelSymbol - Return the symbol to be used for the specified basic
  /// block when its address is taken.  This cannot be its normal LBB label
  /// because the block may be accessed outside its containing function.
  MCSymbol *getAddrLabelSymbol(const BasicBlock *BB);

  /// getAddrLabelSymbolToEmit - Return the symbol to be used for the specified
  /// basic block when its address is taken.  If other blocks were RAUW'd to
  /// this one, we may have to emit them as well, return the whole set.
  std::vector<MCSymbol*> getAddrLabelSymbolToEmit(const BasicBlock *BB);

  /// takeDeletedSymbolsForFunction - If the specified function has had any
  /// references to address-taken blocks generated, but the block got deleted,
  /// return the symbol now so we can emit it.  This prevents emitting a
  /// reference to a symbol that has no definition.
  void takeDeletedSymbolsForFunction(const Function *F,
                                     std::vector<MCSymbol*> &Result);


  //===- EH ---------------------------------------------------------------===//

  /// getOrCreateLandingPadInfo - Find or create an LandingPadInfo for the
  /// specified MachineBasicBlock.
  LandingPadInfo &getOrCreateLandingPadInfo(MachineBasicBlock *LandingPad);

  /// addInvoke - Provide the begin and end labels of an invoke style call and
  /// associate it with a try landing pad block.
  void addInvoke(MachineBasicBlock *LandingPad,
                 MCSymbol *BeginLabel, MCSymbol *EndLabel);

  /// addLandingPad - Add a new panding pad.  Returns the label ID for the
  /// landing pad entry.
  MCSymbol *addLandingPad(MachineBasicBlock *LandingPad);

  /// addPersonality - Provide the personality function for the exception
  /// information.
  void addPersonality(MachineBasicBlock *LandingPad,
                      const Function *Personality);

  /// getPersonalityIndex - Get index of the current personality function inside
  /// Personalitites array
  unsigned getPersonalityIndex() const;

  /// getPersonalities - Return array of personality functions ever seen.
  const std::vector<const Function *>& getPersonalities() const {
    return Personalities;
  }

  /// isUsedFunction - Return true if the functions in the llvm.used list.  This
  /// does not return true for things in llvm.compiler.used unless they are also
  /// in llvm.used.
  bool isUsedFunction(const Function *F) const {
    return UsedFunctions.count(F);
  }

  /// addCatchTypeInfo - Provide the catch typeinfo for a landing pad.
  ///
  void addCatchTypeInfo(MachineBasicBlock *LandingPad,
                        ArrayRef<const GlobalValue *> TyInfo);

  /// addFilterTypeInfo - Provide the filter typeinfo for a landing pad.
  ///
  void addFilterTypeInfo(MachineBasicBlock *LandingPad,
                         ArrayRef<const GlobalValue *> TyInfo);

  /// addCleanup - Add a cleanup action for a landing pad.
  ///
  void addCleanup(MachineBasicBlock *LandingPad);

  /// Add a clause for a landing pad. Returns a new label for the clause. This
  /// is used by EH schemes that have more than one landing pad. In this case,
  /// each clause gets its own basic block.
  MCSymbol *addClauseForLandingPad(MachineBasicBlock *LandingPad);

  /// getTypeIDFor - Return the type id for the specified typeinfo.  This is
  /// function wide.
  unsigned getTypeIDFor(const GlobalValue *TI);

  /// getFilterIDFor - Return the id of the filter encoded by TyIds.  This is
  /// function wide.
  int getFilterIDFor(std::vector<unsigned> &TyIds);

  /// TidyLandingPads - Remap landing pad labels and remove any deleted landing
  /// pads.
  void TidyLandingPads(DenseMap<MCSymbol*, uintptr_t> *LPMap = nullptr);

  /// getLandingPads - Return a reference to the landing pad info for the
  /// current function.
  const std::vector<LandingPadInfo> &getLandingPads() const {
    return LandingPads;
  }

  /// setCallSiteLandingPad - Map the landing pad's EH symbol to the call
  /// site indexes.
  void setCallSiteLandingPad(MCSymbol *Sym, ArrayRef<unsigned> Sites);

  /// getCallSiteLandingPad - Get the call site indexes for a landing pad EH
  /// symbol.
  SmallVectorImpl<unsigned> &getCallSiteLandingPad(MCSymbol *Sym) {
    assert(hasCallSiteLandingPad(Sym) &&
           "missing call site number for landing pad!");
    return LPadToCallSiteMap[Sym];
  }

  /// hasCallSiteLandingPad - Return true if the landing pad Eh symbol has an
  /// associated call site.
  bool hasCallSiteLandingPad(MCSymbol *Sym) {
    return !LPadToCallSiteMap[Sym].empty();
  }

  /// setCallSiteBeginLabel - Map the begin label for a call site.
  void setCallSiteBeginLabel(MCSymbol *BeginLabel, unsigned Site) {
    CallSiteMap[BeginLabel] = Site;
  }

  /// getCallSiteBeginLabel - Get the call site number for a begin label.
  unsigned getCallSiteBeginLabel(MCSymbol *BeginLabel) {
    assert(hasCallSiteBeginLabel(BeginLabel) &&
           "Missing call site number for EH_LABEL!");
    return CallSiteMap[BeginLabel];
  }

  /// hasCallSiteBeginLabel - Return true if the begin label has a call site
  /// number associated with it.
  bool hasCallSiteBeginLabel(MCSymbol *BeginLabel) {
    return CallSiteMap[BeginLabel] != 0;
  }

  /// setCurrentCallSite - Set the call site currently being processed.
  void setCurrentCallSite(unsigned Site) { CurCallSite = Site; }

  /// getCurrentCallSite - Get the call site currently being processed, if any.
  /// return zero if none.
  unsigned getCurrentCallSite() { return CurCallSite; }

  /// getTypeInfos - Return a reference to the C++ typeinfo for the current
  /// function.
  const std::vector<const GlobalValue *> &getTypeInfos() const {
    return TypeInfos;
  }

  /// getFilterIds - Return a reference to the typeids encoding filters used in
  /// the current function.
  const std::vector<unsigned> &getFilterIds() const {
    return FilterIds;
  }

  /// getPersonality - Return a personality function if available.  The presence
  /// of one is required to emit exception handling info.
  const Function *getPersonality() const;

  /// Classify the personality function amongst known EH styles.
  EHPersonality getPersonalityType();

  /// setVariableDbgInfo - Collect information used to emit debugging
  /// information of a variable.
  void setVariableDbgInfo(MDNode *Var, MDNode *Expr, unsigned Slot,
                          DebugLoc Loc) {
    VariableDbgInfos.emplace_back(Var, Expr, Slot, Loc);
  }

  VariableDbgInfoMapTy &getVariableDbgInfo() { return VariableDbgInfos; }

  /// getMachineFunction - Return the MachineFunction associated with the given
  /// function. If no MachineFunction exists, NULL is returned.
  MachineFunction *getMachineFunction(const Function *F) {
    DenseMap<const Function*, MachineFunction*>::iterator tmp(
                                                      MachineFunctions.find(F));
    if (tmp == MachineFunctions.end())
      return NULL;
    else
      return tmp->second;
  }

  /// putMachineFunction - Store a MachineFunction and associate it with the 
  /// given function. This transfers ownership of the MachineFunction to this
  /// class.
  void putMachineFunction(MachineFunction *MF, const Function *F) {
    // TODO is it possible that F is null (i.e. we created a MF in the backend)
    //      If yes, handle this case by keeping those MFs as well.
    assert(F && "Creating a MachineFunction without Function not supported!");

    // check for collisions
    assert(getMachineFunction(F) == MF || getMachineFunction(F) == NULL);

    // store the MachineFunction
    MachineFunctions[F] = MF;
  }


  /// removeMachineFunction - Remove the MachineFunction associated with the 
  /// given function. Ownership is taken away from this class.
  void removeMachineFunction(const Function *F) {
    MachineFunctions.erase(F);
  }

}; // End class MachineModuleInfo

} // End llvm namespace

#endif
