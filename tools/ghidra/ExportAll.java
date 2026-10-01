// Exports functions.tsv, strings.tsv, calls.tsv and decompiled all.c into the directory given as arg 0.
// Arg 1 (optional) "nodecomp" skips decompilation.
// @category Blackbox
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.StringDataInstance;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import java.io.File;
import java.io.PrintWriter;
import java.util.Set;
import java.util.TreeSet;

public class ExportAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        File out = new File(args[0]);
        out.mkdirs();
        boolean decomp = !(args.length > 1 && args[1].equals("nodecomp"));

        try (PrintWriter fw = new PrintWriter(new File(out, "functions.tsv"));
             PrintWriter cw = new PrintWriter(new File(out, "calls.tsv"))) {
            fw.println("entry\tname\tsize\tcallers\tcallees");
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                Set<Function> callers = f.getCallingFunctions(monitor);
                Set<Function> callees = f.getCalledFunctions(monitor);
                fw.printf("0x%08x\t%s\t%d\t%d\t%d%n", f.getEntryPoint().getOffset(), f.getName(),
                        f.getBody().getNumAddresses(), callers.size(), callees.size());
                for (Function c : callees) {
                    cw.printf("0x%08x\t0x%08x%n", f.getEntryPoint().getOffset(), c.getEntryPoint().getOffset());
                }
            }
        }

        try (PrintWriter sw = new PrintWriter(new File(out, "strings.tsv"))) {
            sw.println("addr\tfunctions\ttext");
            DataIterator di = currentProgram.getListing().getDefinedData(true);
            while (di.hasNext() && !monitor.isCancelled()) {
                Data d = di.next();
                if (!d.hasStringValue()) {
                    continue;
                }
                String s = StringDataInstance.getStringDataInstance(d).getStringValue();
                if (s == null) {
                    continue;
                }
                Set<String> users = new TreeSet<>();
                ReferenceIterator ri = currentProgram.getReferenceManager().getReferencesTo(d.getAddress());
                for (Reference r : ri) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f == null) {
                        // literal pool word: follow references to the pool entry
                        for (Reference r2 : getReferencesTo(r.getFromAddress())) {
                            Function f2 = getFunctionContaining(r2.getFromAddress());
                            if (f2 != null) {
                                users.add(String.format("0x%08x", f2.getEntryPoint().getOffset()));
                            }
                        }
                    } else {
                        users.add(String.format("0x%08x", f.getEntryPoint().getOffset()));
                    }
                }
                sw.printf("0x%08x\t%s\t%s%n", d.getAddress().getOffset(), String.join(",", users),
                        s.replace("\t", "\\t").replace("\n", "\\n").replace("\r", "\\r"));
            }
        }

        if (!decomp) {
            return;
        }
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        try (PrintWriter cw = new PrintWriter(new File(out, "all.c"))) {
            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function f = it.next();
                cw.printf("// ==== FUNC 0x%08x %s%n", f.getEntryPoint().getOffset(), f.getName());
                DecompileResults r = di.decompileFunction(f, 60, monitor);
                if (r != null && r.decompileCompleted()) {
                    cw.println(r.getDecompiledFunction().getC());
                } else {
                    cw.println("// decompile failed");
                }
            }
        }
        di.dispose();
    }
}
