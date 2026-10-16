// SPDX-License-Identifier: GPL-3.0-only
// Export function boundaries/calls as TSV for reconstruction planning.
// Arguments: output-file [first-entry-address last-entry-address]
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.stream.Collectors;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

public class ExportInventory extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1 && args.length != 3)
            throw new Exception("Expected output-file [first-entry last-entry]");
        long first = args.length == 3 ? toAddr(args[1]).getOffset() : 0;
        long last = args.length == 3 ? toAddr(args[2]).getOffset() : Long.MAX_VALUE;
        List<String> rows = new ArrayList<>();
        rows.add("entry\tlast\tbytes\tname\tcallees");
        FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
        while (functions.hasNext()) {
            monitor.checkCancelled();
            Function function = functions.next();
            long address = function.getEntryPoint().getOffset();
            if (function.isExternal() || address < first || address > last) continue;
            String calls = function.getCalledFunctions(monitor).stream()
                .sorted(Comparator.comparing(f -> f.getEntryPoint().getOffset()))
                .map(f -> f.getEntryPoint().toString())
                .collect(Collectors.joining(","));
            rows.add(function.getEntryPoint() + "\t" + function.getBody().getMaxAddress()
                + "\t" + function.getBody().getNumAddresses() + "\t"
                + function.getName().replace('\t', ' ') + "\t" + calls);
        }
        Files.write(Path.of(args[0]), rows);
        println("Exported " + (rows.size() - 1) + " functions");
    }
}
