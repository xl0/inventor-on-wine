// Headless query script for tools/decomp.sh. Args: OUTFILE CMD [ARG].
// Addresses print as VA (absolute, image base included) plus RVA.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.PrintWriter;
import java.util.regex.Pattern;

public class Decomp extends GhidraScript {
	PrintWriter out;
	long base;

	String fmt(Address a) {
		return String.format("VA 0x%x RVA 0x%x", a.getOffset(), a.getOffset() - base);
	}

	String fname(Address a) {
		Function f = getFunctionContaining(a);
		return f == null ? "?" : f.getName() + "+0x" + Long.toHexString(a.subtract(f.getEntryPoint()));
	}

	// NAME, or hex number: < image base = RVA, else VA.
	Address resolve(String s) throws Exception {
		if (s.matches("(0x)?[0-9a-fA-F]+")) {
			long v = Long.parseUnsignedLong(s.replaceFirst("^0x", ""), 16);
			boolean rva = v < base;
			Address a = toAddr(rva ? base + v : v);
			out.printf("# %s taken as %s -> %s%n", s, rva ? "RVA" : "VA", fmt(a));
			return a;
		}
		for (Symbol sym : currentProgram.getSymbolTable().getSymbols(s))
			return sym.getAddress();
		throw new Exception("no symbol " + s);
	}

	Pattern pat(String[] args) {
		return Pattern.compile(args.length > 2 ? args[2] : "", Pattern.CASE_INSENSITIVE);
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		out = new PrintWriter(args[0]);
		base = currentProgram.getImageBase().getOffset();
		try {
			query(args);
		} finally {
			out.close();
		}
	}

	void query(String[] args) throws Exception {
		Listing listing = currentProgram.getListing();
		ReferenceManager refs = currentProgram.getReferenceManager();
		switch (args[1]) {
		case "funcs": {
			Pattern p = pat(args);
			for (Function f : listing.getFunctions(true))
				if (p.matcher(f.getName(true)).find())
					out.printf("%s  %6d  %s%n", fmt(f.getEntryPoint()), f.getBody().getNumAddresses(), f.getName(true));
			break;
		}
		case "decomp": {
			Address a = resolve(args[2]);
			Function f = getFunctionContaining(a);
			if (f == null)
				throw new Exception("no function contains " + a);
			out.printf("# %s entry %s%n", f.getName(true), fmt(f.getEntryPoint()));
			DecompInterface di = new DecompInterface();
			di.openProgram(currentProgram);
			DecompileResults r = di.decompileFunction(f, 120, monitor);
			if (!r.decompileCompleted())
				throw new Exception("decompile failed: " + r.getErrorMessage());
			out.print(r.getDecompiledFunction().getC());
			break;
		}
		case "xrefs": {
			Address a = resolve(args[2]);
			Function f = getFunctionAt(a);
			out.printf("# refs to %s%s%n", fmt(a), f == null ? "" : " (" + f.getName(true) + ")");
			for (Reference r : refs.getReferencesTo(a))
				out.printf("%s  %-14s %s%n", fmt(r.getFromAddress()), r.getReferenceType(), fname(r.getFromAddress()));
			break;
		}
		case "strings": {
			Pattern p = pat(args);
			for (Data d : listing.getDefinedData(true)) {
				if (!d.hasStringValue())
					continue;
				String s = String.valueOf(d.getValue());
				if (!p.matcher(s).find())
					continue;
				StringBuilder by = new StringBuilder();
				for (Reference r : refs.getReferencesTo(d.getAddress()))
					by.append(' ').append(fname(r.getFromAddress()));
				out.printf("%s  %s  <-%s%n", fmt(d.getAddress()), s.replace("\n", "\\n").replace("\r", "\\r"), by);
			}
			break;
		}
		case "imports":
			for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
				StringBuilder at = new StringBuilder();
				for (Reference r : refs.getReferencesTo(s.getAddress()))
					at.append(' ').append(String.format("0x%x", r.getFromAddress().getOffset()));
				out.printf("%s  <-%s%n", s.getName(true), at);
			}
			break;
		default:
			throw new Exception("unknown command " + args[1]);
		}
	}
}
