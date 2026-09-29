# 071 Cross-process calls into an STA don't run in the OLE window's creation context (low)
Status: open (draft, low) · Owner: - · Branch: - · Found in: 070 probes

Windows: an STA whose apartment (OLE) window was created while an activation
context was active runs incoming calls from OTHER processes in that context
(probe tests/actctx_comcall, case "STA, marshaled under ctx", cross-process).
Wine (after 070) runs cross-process calls with no context. No known app impact.
