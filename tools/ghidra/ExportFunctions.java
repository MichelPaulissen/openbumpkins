// SPDX-License-Identifier: GPL-3.0-only
// Export selected functions for local research. Arguments: output-directory address ...

import java.nio.file.Files;
import java.nio.file.Path;
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class ExportFunctions extends GhidraScript {
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new Exception("Expected output directory and function addresses");
        Path output = Path.of(args[0]);
        Files.createDirectories(output);
        DecompInterface decompiler = new DecompInterface();
        try {
            if (!decompiler.openProgram(currentProgram)) throw new Exception("Cannot open program");
            for (int i = 1; i < args.length; i++) {
                Address address = toAddr(args[i]);
                Function function = getFunctionAt(address);
                if (function == null) throw new Exception("Missing function: " + address);
                DecompileResults result = decompiler.decompileFunction(function, 60, monitor);
                if (!result.decompileCompleted()) throw new Exception(result.getErrorMessage());
                Files.writeString(output.resolve(address + ".c"), result.getDecompiledFunction().getC());
                println("Exported " + address + " " + function.getSignature());
            }
        } finally {
            decompiler.dispose();
        }
    }
}
