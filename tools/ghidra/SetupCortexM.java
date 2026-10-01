// Pre-script for BLACKBOX.BIN imports: STM32H743 memory map and vector table.
// @category Blackbox
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.listing.Function;

public class SetupCortexM extends GhidraScript {

    private static final long IMAGE_BASE = 0x08040000L;
    // 16 core exceptions + 150 STM32H743 IRQs
    private static final int VECTORS = 166;

    private static final String[] CORE = {
        "initial_sp", "Reset_Handler", "NMI_Handler", "HardFault_Handler",
        "MemManage_Handler", "BusFault_Handler", "UsageFault_Handler", null,
        null, null, null, "SVC_Handler", "DebugMon_Handler", null,
        "PendSV_Handler", "SysTick_Handler"
    };

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        ram(mem, "ITCM", 0x00000000L, 0x10000, true, false);
        ram(mem, "DTCM", 0x20000000L, 0x20000, true, false);
        ram(mem, "AXI_SRAM", 0x24000000L, 0x80000, true, false);
        ram(mem, "SRAM1_3", 0x30000000L, 0x48000, true, false);
        ram(mem, "SRAM4", 0x38000000L, 0x10000, true, false);
        ram(mem, "BKPSRAM", 0x38800000L, 0x1000, true, false);
        ram(mem, "QSPI", 0x90000000L, 0x01000000, true, false);
        ram(mem, "SDRAM", 0xC0000000L, 0x04000000, true, false);
        ram(mem, "APB1", 0x40000000L, 0x10000, true, true);
        ram(mem, "APB2", 0x40010000L, 0x10000, true, true);
        ram(mem, "AHB1", 0x40020000L, 0x60000, true, true);
        ram(mem, "AHB2", 0x48020000L, 0x10000, true, true);
        ram(mem, "APB3_AHB3", 0x50000000L, 0x01000000, true, true);
        ram(mem, "APB4_AHB4", 0x58000000L, 0x01000000, true, true);
        ram(mem, "SCS", 0xE0000000L, 0x00100000, true, true);

        label(0x58024800L, "PWR_CR1");
        label(0x58024808L, "PWR_CR2");
        label(0x58024400L, "RCC_CR");
        label(0x580244D4L, "RCC_AHB4ENR");
        label(0x52007000L, "SDMMC1");
        label(0x48022400L, "SDMMC2");
        label(0x38800000L, "BKPSRAM_base");

        Address base = toAddr(IMAGE_BASE);
        java.util.Map<Long, String> seen = new java.util.HashMap<>();
        for (int i = 0; i < VECTORS; i++) {
            Address slot = base.add(4L * i);
            clearListing(slot, slot.add(3));
            createDWord(slot);
            long v = getInt(slot) & 0xFFFFFFFFL;
            if (i == 0 || v == 0 || (v & 1) == 0) {
                continue;
            }
            String name = i < 16 ? CORE[i] : "IRQ" + (i - 16) + "_Handler";
            if (name == null) {
                name = "Exception" + i + "_Handler";
            }
            long target = v & ~1L;
            if (seen.containsKey(target)) {
                if (!seen.get(target).equals("Default_Handler")) {
                    rename(target, "Default_Handler");
                    seen.put(target, "Default_Handler");
                }
                continue;
            }
            seen.put(target, name);
            Address a = toAddr(target);
            disassemble(a);
            Function f = getFunctionAt(a);
            if (f == null) {
                f = createFunction(a, name);
            } else {
                f.setName(name, SourceType.USER_DEFINED);
            }
        }
        currentProgram.getSymbolTable().createLabel(base, "vector_table", SourceType.USER_DEFINED);

        setAnalysisOption(currentProgram, "ARM Aggressive Instruction Finder", "true");
        setAnalysisOption(currentProgram, "Decompiler Parameter ID", "true");
    }

    private void ram(Memory mem, String name, long start, long len, boolean write, boolean vol) throws Exception {
        Address s = toAddr(start);
        if (mem.getBlock(s) != null) {
            return;
        }
        MemoryBlock b = mem.createUninitializedBlock(name, s, len, false);
        b.setRead(true);
        b.setWrite(write);
        b.setExecute(false);
        b.setVolatile(vol);
    }

    private void label(long addr, String name) throws Exception {
        currentProgram.getSymbolTable().createLabel(toAddr(addr), name, SourceType.USER_DEFINED);
    }

    private void rename(long addr, String name) throws Exception {
        Function f = getFunctionAt(toAddr(addr));
        if (f != null) {
            f.setName(name, SourceType.USER_DEFINED);
        }
    }
}
