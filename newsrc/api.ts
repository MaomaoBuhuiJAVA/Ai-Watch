import type { ChatMessage, PromptCard, Recording, SummaryPayload } from "./types";

const base = () => (import.meta.env.VITE_API_BASE as string | undefined) ?? "";

async function j<T>(path: string, init?: RequestInit): Promise<T> {
  const r = await fetch(`${base()}${path}`, {
    ...init,
    headers: {
      Accept: "application/json",
      ...(init?.headers as Record<string, string>),
    },
  });
  const data = (await r.json().catch(() => ({}))) as T;
  if (!r.ok) {
    const err = new Error((data as { error?: string }).error || r.statusText || "request failed");
    (err as Error & { status: number }).status = r.status;
    throw err;
  }
  return data;
}

export type HealthPayload = { ok: boolean; ts: number };

export const api = {
  health: () => j<HealthPayload>("/health"),
  chatHistory: (limit = 200) => j<{ items: ChatMessage[] }>(`/api/chat/history?limit=${limit}`),
  recordings: (limit = 100) => j<{ items: Recording[] }>(`/api/recordings?limit=${limit}`),
  promptCards: () => j<{ items: PromptCard[] }>("/api/prompt_cards"),
  upsertPromptCard: (body: { slug: string; title: string; body: string; sort_order: number }) =>
    j<{ ok?: boolean }>("/api/prompt_cards", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body),
    }),
  deletePromptCard: (slug: string) =>
    fetch(`${base()}/api/prompt_cards/${encodeURIComponent(slug)}`, { method: "DELETE" }).then(async (r) => {
      if (!r.ok) {
        const d = (await r.json().catch(() => ({}))) as { error?: string };
        throw new Error(d.error || r.statusText);
      }
    }),
  getSystemPrompt: () => j<{ value: string }>("/api/settings/chat_system_prompt"),
  setSystemPrompt: (value: string) =>
    j<{ ok: boolean; value: string }>("/api/settings/chat_system_prompt", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ value }),
    }),
  summarizeRecording: (rid: number, prompt_card_slug?: string) =>
    j<{ summary: SummaryPayload }>(`/api/recordings/${rid}/summarize`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ prompt_card_slug: prompt_card_slug || "" }),
    }),
};

export function recordingFileUrl(id: number, kind: "wav" | "mp3" | "txt") {
  return `${base()}/api/recordings/${id}/file?kind=${kind}`;
}

export function recordingExportUrl(id: number, format: "docx" | "pdf") {
  return `${base()}/api/recordings/${id}/export?format=${format}`;
}
