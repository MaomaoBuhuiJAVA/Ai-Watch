export type HealthStatus = { ok: boolean; ts: number };

export type ChatMessage = {
  id: number;
  device_name: string;
  role: string;
  content: string;
  source: string;
  created_at: number;
};

export type Recording = {
  id: number;
  path: string;
  device_name: string;
  transcript: string;
  txt_path: string;
  mp3_path: string;
  category: string;
  summary_json: string;
  created_at: number;
};

export type SummaryPayload = {
  title?: string;
  report?: string;
  highlights?: string[];
};

export type PromptCard = {
  id?: number;
  slug: string;
  title: string;
  body: string;
  sort_order: number;
  created_at?: number;
  isDefault?: boolean;
};
