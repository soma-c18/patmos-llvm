#include "DWARFAcceleratorTable.h"

#include "llvm/Support/Dwarf.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {

bool DWARFAcceleratorTable::extract() {
  uint32_t Offset = 0;

  // Check that we can at least read the header.
  if (!AccelSection.isValidOffset(offsetof(Header, HeaderDataLength)+4))
    return false;

  Hdr.Magic = AccelSection.getU32(&Offset);
  Hdr.Version = AccelSection.getU16(&Offset);
  Hdr.HashFunction = AccelSection.getU16(&Offset);
  Hdr.NumBuckets = AccelSection.getU32(&Offset);
  Hdr.NumHashes = AccelSection.getU32(&Offset);
  Hdr.HeaderDataLength = AccelSection.getU32(&Offset);

  // Check that we can read all the hashes and offsets from the
  // section (see SourceLevelDebugging.rst for the structure of the index).
  if (!AccelSection.isValidOffset(sizeof(Hdr) + Hdr.HeaderDataLength +
                                  Hdr.NumBuckets*4 + Hdr.NumHashes*8))
    return false;

  HdrData.DIEOffsetBase = AccelSection.getU32(&Offset);
  uint32_t NumAtoms = AccelSection.getU32(&Offset);

  for (unsigned i = 0; i < NumAtoms; ++i) {
    auto Atom = std::make_pair(AccelSection.getU16(&Offset),
                               DWARFFormValue(AccelSection.getU16(&Offset)));
    HdrData.Atoms.push_back(Atom);
  }

  return true;
}

void DWARFAcceleratorTable::dump(raw_ostream &OS) {
  // Dump the header.
  OS << "Magic = " << format("0x%08x", Hdr.Magic) << '\n';
  OS << "Version = " << format("0x%04x", Hdr.Version) << '\n';
  OS << "Hash function = " << format("0x%08x", Hdr.HashFunction) << '\n';
  OS << "Bucket count = " << Hdr.NumBuckets << '\n';
  OS << "Hashes count = " << Hdr.NumHashes << '\n';
  OS << "HeaderData length = " << Hdr.HeaderDataLength << '\n';
  OS << "DIE offset base = " << HdrData.DIEOffsetBase << '\n';
  OS << "Number of atoms = " << HdrData.Atoms.size() << '\n';

  unsigned i = 0;
  for (const auto &Atom: HdrData.Atoms) {
    OS << format("Atom[%d] ", i++);
    OS << " Type: " << dwarf::AtomTypeString(Atom.first);
    OS << " Form: " << dwarf::FormEncodingString(Atom.second.getForm());
    OS << "\n";
  }

  // Now go through the actual tables and dump them.
  uint32_t Offset = sizeof(Hdr) + Hdr.HeaderDataLength;
  unsigned HashesBase = Offset + Hdr.NumBuckets * 4;
  unsigned OffsetsBase = HashesBase + Hdr.NumHashes * 4;

  for (unsigned Bucket = 0; Bucket < Hdr.NumBuckets; ++Bucket) {
    unsigned Index;
    Index = AccelSection.getU32(&Offset);

    OS << format("Bucket[%d]\n", Bucket);
    if (Index == UINT32_MAX) {
      OS << "  EMPTY\n";
      continue;
    }

    for (unsigned HashIdx = Index; HashIdx < Hdr.NumHashes; ++HashIdx) {
      unsigned HashOffset = HashesBase + HashIdx*4;
      unsigned OffsetsOffset = OffsetsBase + HashIdx*4;
      uint32_t Hash = AccelSection.getU32(&HashOffset);

      if (Hash % Hdr.NumBuckets != Bucket)
        break;

      unsigned DataOffset = AccelSection.getU32(&OffsetsOffset);
      OS << format("  Hash = 0x%08x Offset = 0x%08x\n", Hash, DataOffset);
      if (!AccelSection.isValidOffset(DataOffset)) {
        OS << "    Invalid section offset\n";
        continue;
      }
      while (unsigned StringOffset = AccelSection.getU32(&DataOffset)) {
        OS << format("    Name: %08x \"%s\"\n", StringOffset,
                     StringSection.getCStr(&StringOffset));
        unsigned NumData = AccelSection.getU32(&DataOffset);
        for (unsigned Data = 0; Data < NumData; ++Data) {
          OS << format("    Data[%d] => ", Data);
          unsigned i = 0;
          for (auto &Atom : HdrData.Atoms) {
            OS << format("{Atom[%d]: ", i++);
            if (Atom.second.extractValue(AccelSection, &DataOffset, nullptr))
              Atom.second.dump(OS, nullptr);
            else
              OS << "Error extracting the value";
            OS << "} ";
          }
          OS << '\n';
        }
      }
    }
  }
}
}
