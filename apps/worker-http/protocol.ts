// The channel records between the UI and the worker.
//   request (UI -> worker, 1 byte): the target index.
//   progress (worker -> UI, 16 bytes, little endian): phase u32, bytes u32,
//     resource status i32, HTTP status u16, target u8.
export const TARGETS = ["https://example.com/", "http://example.com/"] as const;
export const PHASES = ["Ready", "Request accepted", "Headers received", "Receiving body", "Complete", "Failed"] as const;
export const Phase = { ready: 0, accepted: 1, headers: 2, body: 3, complete: 4, failed: 5 } as const;
export const PROGRESS_BYTES = 16;

export interface Progress {
  phase: number;
  bytes: number;
  status: number;
  code: number;
  target: number;
}

export function encodeProgress(progress: Progress, into = new Uint8Array(PROGRESS_BYTES)): Uint8Array {
  const view = new DataView(into.buffer, into.byteOffset, into.byteLength);
  view.setUint32(0, progress.phase, true);
  view.setUint32(4, progress.bytes, true);
  view.setInt32(8, progress.status, true);
  view.setUint16(12, progress.code, true);
  view.setUint8(14, progress.target);
  return into;
}

export function decodeProgress(bytes: Uint8Array): Progress {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  return { phase: view.getUint32(0, true), bytes: view.getUint32(4, true), status: view.getInt32(8, true),
    code: view.getUint16(12, true), target: view.getUint8(14) };
}
