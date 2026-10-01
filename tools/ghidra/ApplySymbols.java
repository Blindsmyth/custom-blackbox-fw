// Applies names from a CSV: address,name[,comment]. Lines starting with # are ignored.
// Missing functions are created at the address; non-code addresses get a label.
// @category Blackbox
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

public class ApplySymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1 || !Files.exists(Paths.get(args[0]))) {
            println("ApplySymbols: no CSV, skipping");
            return;
        }
        List<String> lines = Files.readAllLines(Paths.get(args[0]));
        int n = 0;
        for (String line : lines) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#") || line.startsWith("address,")) {
                continue;
            }
            String[] p = line.split(",", 3);
            if (p.length < 2) {
                continue;
            }
            Address a = toAddr(Long.decode(p[0].trim()) & ~1L);
            String name = p[1].trim();
            String kind = "";
            String comment = p.length > 2 ? p[2].trim() : "";
            if (comment.startsWith("data;")) {
                kind = "data";
                comment = comment.substring(5).trim();
            }
            if (!kind.equals("data") && currentProgram.getMemory().getBlock(a) != null
                    && currentProgram.getMemory().getBlock(a).isInitialized()) {
                Function f = getFunctionAt(a);
                if (f == null) {
                    disassemble(a);
                    f = createFunction(a, name);
                }
                if (f != null) {
                    f.setName(name, SourceType.USER_DEFINED);
                    if (!comment.isEmpty()) {
                        f.setComment(comment);
                    }
                    n++;
                    continue;
                }
            }
            currentProgram.getSymbolTable().createLabel(a, name, SourceType.USER_DEFINED);
            if (!comment.isEmpty()) {
                currentProgram.getListing().setComment(a, CodeUnit.PLATE_COMMENT, comment);
            }
            n++;
        }
        println("ApplySymbols: applied " + n);
    }
}
