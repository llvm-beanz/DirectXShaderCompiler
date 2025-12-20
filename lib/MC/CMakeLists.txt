# HLSL Change Begin - Strip MC down to minimal set

set(HLSL_IGNORE_SOURCES ConstantPools.cpp
  ELFObjectWriter.cpp
  MCAsmBackend.cpp
  MCAsmInfo.cpp
  MCAsmInfoCOFF.cpp
  MCAsmInfoDarwin.cpp
  MCAsmInfoELF.cpp
  MCAsmStreamer.cpp
  MCAssembler.cpp
  MCCodeEmitter.cpp
  MCCodeGenInfo.cpp
  MCContext.cpp
  MCDwarf.cpp
  MCELFObjectTargetWriter.cpp
  MCELFStreamer.cpp
  MCExpr.cpp
  MCInst.cpp
  MCInstPrinter.cpp
  MCInstrAnalysis.cpp
  MCInstrDesc.cpp
  MCLabel.cpp
  MCLinkerOptimizationHint.cpp
  MCMachOStreamer.cpp
  MCMachObjectTargetWriter.cpp
  MCNullStreamer.cpp
  MCObjectFileInfo.cpp
  MCObjectStreamer.cpp
  MCObjectWriter.cpp
  MCRegisterInfo.cpp
  MCSchedule.cpp
  MCSection.cpp
  MCSectionCOFF.cpp
  MCSectionELF.cpp
  MCSectionMachO.cpp
  MCStreamer.cpp
  MCSubtargetInfo.cpp
  MCSymbol.cpp
  MCSymbolELF.cpp
  MCSymbolizer.cpp
  MCTargetOptions.cpp
  MCValue.cpp
  MCWin64EH.cpp
  MCWinEH.cpp
  MachObjectWriter.cpp
  SubtargetFeature.cpp
  WinCOFFObjectWriter.cpp
  WinCOFFStreamer.cpp
  YAML.cpp)

add_llvm_library(LLVMMC  
  # ConstantPools.cpp
  # ELFObjectWriter.cpp
  # MCAsmBackend.cpp
  # MCAsmInfo.cpp
  # MCAsmInfoCOFF.cpp
  # MCAsmInfoDarwin.cpp
  # MCAsmInfoELF.cpp
  # MCAsmStreamer.cpp
  # MCAssembler.cpp
  # MCCodeEmitter.cpp
  # MCCodeGenInfo.cpp
  # MCContext.cpp
  # MCDwarf.cpp
  # MCELFObjectTargetWriter.cpp
  # MCELFStreamer.cpp
  # MCExpr.cpp
  # MCInst.cpp
  # MCInstPrinter.cpp
  # MCInstrAnalysis.cpp
  # MCInstrDesc.cpp
  # MCLabel.cpp
  # MCLinkerOptimizationHint.cpp
  # MCMachOStreamer.cpp
  # MCMachObjectTargetWriter.cpp
  # MCNullStreamer.cpp
  # MCObjectFileInfo.cpp
  # MCObjectStreamer.cpp
  # MCObjectWriter.cpp
  # MCRegisterInfo.cpp
  # MCSchedule.cpp
  # MCSection.cpp
  # MCSectionCOFF.cpp
  # MCSectionELF.cpp
  # MCSectionMachO.cpp
  # MCStreamer.cpp
  # MCSubtargetInfo.cpp
  # MCSymbol.cpp
  # MCSymbolELF.cpp
  # MCSymbolizer.cpp
  # MCTargetOptions.cpp
  # MCValue.cpp
  # MCWin64EH.cpp
  # MCWinEH.cpp
  # MachObjectWriter.cpp
  StringTableBuilder.cpp
  # SubtargetFeature.cpp
  # WinCOFFObjectWriter.cpp
  # WinCOFFStreamer.cpp
  # YAML.cpp
  # HLSL Change End

  ADDITIONAL_HEADER_DIRS
  ${LLVM_MAIN_INCLUDE_DIR}/llvm/MC
  )

add_subdirectory(MCParser)
add_subdirectory(MCDisassembler)
