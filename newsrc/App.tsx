import React, { useCallback, useEffect, useMemo, useState } from "react";
import { motion, AnimatePresence } from "framer-motion";
import {
  Activity,
  MessageSquare,
  Mic,
  ExternalLink,
  RefreshCcw,
  FileText,
  Download,
  Trash2,
  Plus,
  Search,
  List,
  X,
  User,
  Cpu,
  Sparkles,
  LayoutDashboard,
  FileDown,
  Clock,
  Maximize2,
} from "lucide-react";
import {
  XAxis,
  YAxis,
  CartesianGrid,
  Tooltip,
  ResponsiveContainer,
  AreaChart,
  Area,
} from "recharts";
import { cn } from "./lib/utils";
import { api, recordingFileUrl } from "./api";
import type { ChatMessage, HealthStatus, PromptCard, Recording, SummaryPayload } from "./types";

type TabId = "overview" | "prompts" | "chat" | "recordings";

function useHealthPoll() {
  const [health, setHealth] = useState<HealthStatus | null>(null);
  const [bad, setBad] = useState(false);
  const tick = useCallback(async () => {
    try {
      const h = await api.health();
      setHealth({ ok: h.ok, ts: h.ts });
      setBad(false);
    } catch {
      setBad(true);
    }
  }, []);
  useEffect(() => {
    void tick();
    const id = setInterval(tick, 15000);
    return () => clearInterval(id);
  }, [tick]);
  return { health, bad, refresh: tick };
}

function BackgroundOrbs() {
  return (
    <div className="pointer-events-none fixed inset-0 z-0 overflow-hidden">
      <motion.div
        className="absolute -left-[15%] -top-[18%] h-[58%] w-[58%] rounded-full bg-[#3B82F6]/[0.12] blur-[120px]"
        animate={{
          scale: [1, 1.15, 1.05, 1],
          rotate: [0, 25, -8, 0],
          x: [0, 40, -20, 0],
          y: [0, 30, -25, 0],
        }}
        transition={{ duration: 28, repeat: Infinity, ease: "easeInOut" }}
      />
      <motion.div
        className="absolute -bottom-[22%] -right-[12%] h-[65%] w-[65%] rounded-full bg-indigo-600/[0.11] blur-[140px]"
        animate={{
          scale: [1.1, 1, 1.18, 1.1],
          rotate: [0, -35, 12, 0],
          x: [0, -50, 25, 0],
          y: [0, -40, 35, 0],
        }}
        transition={{ duration: 34, repeat: Infinity, ease: "easeInOut" }}
      />
    </div>
  );
}

function GlassHeader({
  health,
  bad,
}: {
  health: HealthStatus | null;
  bad: boolean;
}) {
  return (
    <header className="sticky top-0 z-[100] glass-dark border-b border-white/5">
      <div className="mx-auto flex h-16 max-w-7xl items-center justify-between px-6">
        <div className="flex items-center gap-6">
          <div className="flex h-9 w-9 items-center justify-center rounded-2xl bg-brand shadow-[0_0_22px_rgba(59,130,246,0.45)]">
            <Cpu className="h-5 w-5 text-white" />
          </div>
          <nav className="hidden items-center gap-6 md:flex">
            <a
              href="/docs"
              target="_blank"
              rel="noreferrer"
              className="flex items-center gap-1.5 text-xs font-semibold text-slate-500 transition-colors hover:text-white"
            >
              OpenAPI <ExternalLink className="h-3 w-3" />
            </a>
            <div className="h-3 w-px bg-white/10" />
            <div className="flex items-center gap-2">
              <motion.span
                className={cn(
                  "inline-block h-2 w-2 rounded-full",
                  bad ? "bg-rose-500" : "bg-emerald-400",
                )}
                animate={bad ? {} : { scale: [1, 1.35, 1], opacity: [1, 0.75, 1] }}
                transition={{ duration: 1.8, repeat: Infinity, ease: "easeInOut" }}
              />
              <span className="text-[10px] font-bold uppercase tracking-widest text-slate-400">
                {bad ? "离线" : "服务在线"}
              </span>
            </div>
          </nav>
        </div>
        <div className="flex items-center gap-4">
          {health && (
            <div className="flex flex-col items-end">
              <span className="mb-0.5 text-[10px] font-bold uppercase tracking-widest text-slate-500">
                同步时间
              </span>
              <span className="font-mono text-[11px] text-slate-400">
                {new Date(health.ts * 1000).toLocaleString("zh-CN", { hour12: false })}
              </span>
            </div>
          )}
          <div className="flex h-10 w-10 cursor-pointer items-center justify-center rounded-full border border-white/10 bg-white/5 text-slate-400 transition-colors hover:text-white">
            <User className="h-5 w-5" />
          </div>
        </div>
      </div>
    </header>
  );
}

function DeckTabs({ active, onChange }: { active: TabId; onChange: (t: TabId) => void }) {
  const tabs: { id: TabId; label: string; icon: typeof Sparkles }[] = [
    { id: "overview", label: "总览", icon: LayoutDashboard },
    { id: "prompts", label: "提示词研究室", icon: Sparkles },
    { id: "chat", label: "对话档案", icon: MessageSquare },
    { id: "recordings", label: "音频金库", icon: Mic },
  ];
  return (
    <div className="flex shrink-0 rounded-2xl border border-white/10 bg-white/5 p-1 backdrop-blur-3xl">
      {tabs.map((tab) => (
        <button
          key={tab.id}
          type="button"
          onClick={() => onChange(tab.id)}
          className={cn(
            "relative flex items-center gap-2 rounded-xl px-5 py-2.5 text-sm font-bold transition-all duration-300 md:px-6",
            active === tab.id ? "text-white" : "text-slate-500 hover:text-slate-300",
          )}
        >
          {active === tab.id && (
            <motion.div
              layoutId="deskTabPill"
              className="absolute inset-0 rounded-xl bg-brand shadow-[0_4px_24px_rgba(59,130,246,0.35)]"
              transition={{ type: "spring", bounce: 0.2, duration: 0.55 }}
            />
          )}
          <tab.icon className="relative z-10 h-4 w-4" />
          <span className="relative z-10 hidden sm:inline">{tab.label}</span>
        </button>
      ))}
    </div>
  );
}

function OverviewPanel() {
  const [value, setValue] = useState("");
  const [msg, setMsg] = useState("");
  const load = useCallback(async () => {
    setMsg("");
    try {
      const r = await api.getSystemPrompt();
      setValue(r.value || "");
    } catch (e) {
      setMsg((e as Error).message);
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);
  const save = async () => {
    setMsg("保存中…");
    try {
      await api.setSystemPrompt(value);
      setMsg("已保存");
    } catch (e) {
      setMsg((e as Error).message);
    }
  };
  return (
    <div className="grid gap-8 lg:grid-cols-2">
      <div className="glass-card rounded-3xl border border-white/5 p-8">
        <h2 className="mb-2 text-xl font-black tracking-tight text-white">全局系统提示词</h2>
        <p className="mb-6 text-sm text-slate-500">手表端文本 / 语音对话默认 system，写入服务端 SQLite。</p>
        <textarea
          value={value}
          onChange={(e) => setValue(e.target.value)}
          className="mb-4 min-h-[220px] w-full rounded-2xl border border-white/10 bg-[#0a0a0f] p-4 text-sm text-slate-200 outline-none transition-colors focus:border-brand/40"
          placeholder="加载中…"
        />
        <div className="flex flex-wrap gap-3">
          <button
            type="button"
            onClick={() => void save()}
            className="rounded-2xl bg-brand px-6 py-3 text-sm font-bold text-white shadow-lg shadow-brand/25 transition-transform hover:scale-[1.02] active:scale-[0.98]"
          >
            保存
          </button>
          <button
            type="button"
            onClick={() => void load()}
            className="rounded-2xl border border-white/10 bg-white/5 px-6 py-3 text-sm font-bold text-slate-200 hover:bg-white/10"
          >
            重新加载
          </button>
        </div>
        {msg && <p className="mt-3 text-sm text-slate-500">{msg}</p>}
      </div>
      <div className="glass-card rounded-3xl border border-white/5 p-8">
        <h2 className="mb-3 text-xl font-black tracking-tight text-white">流水线说明</h2>
        <ul className="space-y-3 text-sm leading-relaxed text-slate-400">
          <li>录音上传后自动转写、同目录 TXT；本机有 ffmpeg 时生成 MP3。</li>
          <li>AI 分类：工作计划 / 语音复盘 / 其他。</li>
          <li>在「音频金库」可选提示词卡片并生成总结（标题、要点高亮、报告）。</li>
        </ul>
      </div>
    </div>
  );
}

type UICard = PromptCard & { isDefault?: boolean };

function CodeTextarea(props: React.TextareaHTMLAttributes<HTMLTextAreaElement>) {
  return (
    <textarea
      spellCheck={false}
      {...props}
      className={cn(
        "min-h-[12rem] w-full rounded-2xl border border-white/10 bg-[#08080c] p-4 font-mono text-[13px] leading-relaxed text-sky-100/90 caret-brand",
        "shadow-inner shadow-black/40 selection:bg-brand/30 selection:text-white",
        "outline-none transition-colors focus:border-brand/45 focus:ring-1 focus:ring-brand/25",
        props.className,
      )}
    />
  );
}

function PromptModal({
  card,
  isNew,
  onClose,
  onSave,
}: {
  card: UICard;
  isNew: boolean;
  onClose: () => void;
  onSave: (c: UICard) => void;
}) {
  const [form, setForm] = useState<UICard>({ ...card });
  return (
    <div className="fixed inset-0 z-[200] flex items-center justify-center p-6">
      <motion.button
        type="button"
        aria-label="关闭"
        initial={{ opacity: 0 }}
        animate={{ opacity: 1 }}
        exit={{ opacity: 0 }}
        onClick={onClose}
        className="absolute inset-0 border-0 bg-black/75 backdrop-blur-md"
      />
      <motion.div
        initial={{ opacity: 0, scale: 0.96, y: 16 }}
        animate={{ opacity: 1, scale: 1, y: 0 }}
        exit={{ opacity: 0, scale: 0.96, y: 16 }}
        transition={{ type: "spring", damping: 26, stiffness: 320 }}
        className="relative w-full max-w-xl overflow-hidden rounded-[2rem] border border-white/10 glass-dark"
      >
        <div className="flex items-center justify-between border-b border-white/5 p-8">
          <div>
            <h3 className="text-2xl font-black tracking-tight text-white">
              {isNew ? "新建卡片" : "编辑提示词"}
            </h3>
            <p className="mt-1 text-xs font-medium text-slate-500">Slug 唯一；保存时同 slug 覆盖</p>
          </div>
          <button type="button" onClick={onClose} className="rounded-xl border border-white/10 bg-white/5 p-2 text-slate-400 hover:text-white">
            <X className="h-5 w-5" />
          </button>
        </div>
        <div className="space-y-5 p-8">
          <div>
            <label className="mb-1 ml-1 block text-[10px] font-bold uppercase tracking-widest text-slate-500">Slug</label>
            <input
              readOnly={!isNew}
              value={form.slug}
              onChange={(e) => setForm({ ...form, slug: e.target.value })}
              className="w-full rounded-2xl border border-white/10 bg-white/5 px-4 py-3 font-mono text-sm outline-none focus:border-brand/40"
              placeholder="meeting_notes"
            />
          </div>
          <div>
            <label className="mb-1 ml-1 block text-[10px] font-bold uppercase tracking-widest text-slate-500">标题</label>
            <input
              value={form.title}
              onChange={(e) => setForm({ ...form, title: e.target.value })}
              className="w-full rounded-2xl border border-white/10 bg-white/5 px-4 py-3 text-sm outline-none focus:border-brand/40"
            />
          </div>
          <div>
            <label className="mb-1 ml-1 block text-[10px] font-bold uppercase tracking-widest text-slate-500">正文（system / 说明）</label>
            <CodeTextarea value={form.body} onChange={(e) => setForm({ ...form, body: e.target.value })} />
          </div>
          <div>
            <label className="mb-1 ml-1 block text-[10px] font-bold uppercase tracking-widest text-slate-500">排序</label>
            <input
              type="number"
              value={form.sort_order}
              onChange={(e) => setForm({ ...form, sort_order: parseInt(e.target.value, 10) || 0 })}
              className="w-full rounded-2xl border border-white/10 bg-white/5 px-4 py-3 text-sm outline-none focus:border-brand/40"
            />
          </div>
        </div>
        <div className="flex gap-3 border-t border-white/5 bg-white/[0.03] p-8">
          <button type="button" onClick={onClose} className="flex-1 rounded-2xl border border-white/10 py-3 text-sm font-bold text-slate-300 hover:bg-white/5">
            取消
          </button>
          <button
            type="button"
            onClick={() => onSave(form)}
            className="flex-[2] rounded-2xl bg-brand py-3 text-sm font-bold text-white shadow-lg shadow-brand/30 hover:scale-[1.01] active:scale-[0.99]"
          >
            保存
          </button>
        </div>
      </motion.div>
    </div>
  );
}

function PromptStudio({ onCardsChange }: { onCardsChange?: () => void }) {
  const [cards, setCards] = useState<UICard[]>([]);
  const [editing, setEditing] = useState<UICard | null>(null);
  const [isNew, setIsNew] = useState(false);
  const [err, setErr] = useState("");

  const load = useCallback(async () => {
    setErr("");
    try {
      const r = await api.promptCards();
      setCards(
        (r.items || []).map((c) => ({
          ...c,
          isDefault: c.slug === "default",
        })),
      );
    } catch (e) {
      setErr((e as Error).message);
    }
  }, []);

  useEffect(() => {
    void load();
  }, [load]);

  const icons = [Sparkles, Cpu, Activity, MessageSquare, Mic, FileText];

  const saveRemote = async (c: UICard) => {
    await api.upsertPromptCard({
      slug: c.slug.trim(),
      title: c.title.trim(),
      body: c.body,
      sort_order: c.sort_order,
    });
    await load();
    onCardsChange?.();
    setEditing(null);
  };

  const del = async (slug: string) => {
    if (!confirm(`删除卡片 ${slug} ?`)) return;
    try {
      await api.deletePromptCard(slug);
      await load();
      onCardsChange?.();
    } catch (e) {
      alert((e as Error).message);
    }
  };

  return (
    <div className="space-y-8">
      <div className="flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex items-center gap-3">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-brand/15 text-brand">
            <Sparkles className="h-6 w-6" />
          </div>
          <div>
            <h2 className="text-2xl font-black tracking-tight text-white">提示词研究室</h2>
            <p className="text-sm text-slate-500">物理卡片 · 弹出层编辑 · 对接 /api/prompt_cards</p>
          </div>
        </div>
        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            onClick={() => void load()}
            className="flex items-center gap-2 rounded-2xl border border-white/10 bg-white/5 px-4 py-2.5 text-sm font-bold text-slate-200 hover:bg-white/10"
          >
            <RefreshCcw className="h-4 w-4" /> 刷新
          </button>
          <button
            type="button"
            onClick={() => {
              setEditing({
                slug: "",
                title: "",
                body: "",
                sort_order: cards.length ? Math.max(...cards.map((c) => c.sort_order)) + 1 : 0,
              });
              setIsNew(true);
            }}
            className="flex items-center gap-2 rounded-2xl bg-white px-5 py-2.5 text-sm font-black text-black shadow-xl transition-transform hover:scale-[1.02] active:scale-[0.98]"
          >
            <Plus className="h-5 w-5" /> 新建卡片
          </button>
        </div>
      </div>
      {err && <p className="text-sm text-rose-400">{err}</p>}
      <div className="grid gap-6 md:grid-cols-2 lg:grid-cols-3">
        {cards.map((card, i) => {
          const Icon = icons[i % icons.length];
          return (
            <motion.div
              layout
              key={card.slug}
              whileHover={{ y: -4 }}
              className="glass-card flex min-h-[300px] flex-col rounded-[2rem] p-8"
            >
              <div className="mb-6 flex items-start justify-between">
                <div className="flex h-14 w-14 items-center justify-center rounded-2xl border border-white/10 bg-white/5 text-brand transition-colors group-hover:bg-brand/15">
                  <Icon className="h-8 w-8" />
                </div>
                {!card.isDefault && (
                  <button
                    type="button"
                    onClick={() => void del(card.slug)}
                    className="p-2 text-slate-600 transition-colors hover:text-rose-400"
                  >
                    <Trash2 className="h-4 w-4" />
                  </button>
                )}
              </div>
              <h3 className="text-xl font-bold text-white">{card.title}</h3>
              <p className="mt-1 font-mono text-[10px] uppercase tracking-widest text-slate-500">slug · {card.slug}</p>
              <p className="mt-3 line-clamp-4 flex-1 text-sm leading-relaxed text-slate-400">{card.body}</p>
              <div className="mt-8 flex gap-3 border-t border-white/5 pt-6">
                <button
                  type="button"
                  onClick={() => {
                    setEditing({ ...card });
                    setIsNew(false);
                  }}
                  className="flex-1 rounded-xl border border-white/10 py-3 text-xs font-bold text-slate-200 transition-colors hover:border-brand/30 hover:text-white"
                >
                  编辑
                </button>
              </div>
            </motion.div>
          );
        })}
      </div>
      <AnimatePresence>
        {editing && (
          <PromptModal
            card={editing}
            isNew={isNew}
            onClose={() => setEditing(null)}
            onSave={async (c) => {
              if (!c.slug.trim() || !c.title.trim()) {
                alert("请填写 slug 与标题");
                return;
              }
              try {
                await saveRemote(c);
              } catch (e) {
                alert((e as Error).message);
              }
            }}
          />
        )}
      </AnimatePresence>
    </div>
  );
}

function RoleBadge({ role }: { role: string }) {
  const styles: Record<string, string> = {
    user: "border-white/10 bg-white/5 text-slate-400",
    assistant: "border-brand/25 bg-brand/10 text-brand",
  };
  return (
    <span className={cn("rounded-full border px-3 py-1 text-[10px] font-bold uppercase tracking-wider", styles[role] || "bg-slate-800 text-slate-300")}>
      {role}
    </span>
  );
}

function buildActivityChart(items: ChatMessage[]) {
  const hours = [8, 10, 12, 14, 16, 18, 20];
  const map = new Map<number, number>();
  for (const h of hours) map.set(h, 0);
  for (const m of items) {
    const h = new Date(m.created_at * 1000).getHours();
    if (map.has(h)) map.set(h, (map.get(h) || 0) + 1);
  }
  return hours.map((h) => ({ name: `${h}:00`, value: map.get(h) || 0 }));
}

function ChatArchive() {
  const [view, setView] = useState<"table" | "bubbles">("table");
  const [search, setSearch] = useState("");
  const [items, setItems] = useState<ChatMessage[]>([]);
  const load = useCallback(async () => {
    try {
      const r = await api.chatHistory(200);
      setItems(r.items || []);
    } catch {
      setItems([]);
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);

  const filtered = useMemo(() => {
    const q = search.toLowerCase();
    return items.filter((c) => c.content.toLowerCase().includes(q) || (c.device_name || "").toLowerCase().includes(q));
  }, [items, search]);

  const bubbles = useMemo(() => [...filtered].reverse(), [filtered]);
  const chartData = useMemo(() => buildActivityChart(items), [items]);

  return (
    <div className="space-y-8">
      <div className="flex flex-col gap-4 lg:flex-row lg:items-center lg:justify-between">
        <div className="flex w-full flex-1 flex-col gap-3 sm:flex-row lg:max-w-xl">
          <div className="relative flex-1">
            <Search className="pointer-events-none absolute left-4 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-500" />
            <input
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              className="w-full rounded-2xl border border-white/10 bg-white/5 py-3 pl-11 pr-4 text-sm outline-none focus:border-brand/40"
              placeholder="搜索内容或设备…"
            />
          </div>
          <button
            type="button"
            onClick={() => void load()}
            className="flex items-center justify-center gap-2 rounded-2xl border border-white/10 bg-white/5 px-4 py-3 text-sm font-bold hover:bg-white/10"
          >
            <RefreshCcw className="h-4 w-4" /> 刷新
          </button>
        </div>
        <div className="flex items-center rounded-xl border border-white/10 bg-white/5 p-1">
          <button
            type="button"
            onClick={() => setView("table")}
            className={cn("rounded-lg p-2 transition-colors", view === "table" ? "bg-brand text-white" : "text-slate-500 hover:text-white")}
          >
            <List className="h-4 w-4" />
          </button>
          <button
            type="button"
            onClick={() => setView("bubbles")}
            className={cn("rounded-lg p-2 transition-colors", view === "bubbles" ? "bg-brand text-white" : "text-slate-500 hover:text-white")}
          >
            <MessageSquare className="h-4 w-4" />
          </button>
        </div>
      </div>

      <div className="overflow-hidden rounded-[2rem] border border-white/5 glass-dark">
        {view === "table" ? (
          <div className="overflow-x-auto">
            <table className="w-full text-left text-sm">
              <thead className="border-b border-white/5 bg-white/[0.02]">
                <tr>
                  <th className="px-6 py-4 text-[10px] font-bold uppercase tracking-widest text-slate-500">时间</th>
                  <th className="px-6 py-4 text-[10px] font-bold uppercase tracking-widest text-slate-500">设备</th>
                  <th className="px-6 py-4 text-[10px] font-bold uppercase tracking-widest text-slate-500">角色</th>
                  <th className="px-6 py-4 text-[10px] font-bold uppercase tracking-widest text-slate-500">来源</th>
                  <th className="px-6 py-4 text-[10px] font-bold uppercase tracking-widest text-slate-500">内容</th>
                </tr>
              </thead>
              <tbody className="divide-y divide-white/5">
                {filtered.map((chat) => (
                  <tr key={chat.id} className="hover:bg-white/[0.02]">
                    <td className="whitespace-nowrap px-6 py-4 font-mono text-xs text-slate-500">
                      {new Date(chat.created_at * 1000).toLocaleString("zh-CN", { hour12: false })}
                    </td>
                    <td className="px-6 py-4 font-semibold text-slate-300">{chat.device_name}</td>
                    <td className="px-6 py-4">
                      <RoleBadge role={chat.role} />
                    </td>
                    <td className="px-6 py-4 text-slate-500">{chat.source}</td>
                    <td className="max-w-md truncate px-6 py-4 text-slate-400">{chat.content}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        ) : (
          <div className="no-scrollbar max-h-[560px] space-y-6 overflow-y-auto p-10">
            {bubbles.map((chat) => (
              <div
                key={chat.id}
                className={cn("flex max-w-[78%] flex-col gap-1", chat.role === "assistant" ? "mr-auto" : "ml-auto items-end")}
              >
                <div className="flex items-center gap-2 text-[10px] font-bold uppercase tracking-wider text-slate-600">
                  {chat.role === "assistant" && (
                    <span className="flex h-6 w-6 items-center justify-center rounded-full bg-brand/20 text-[9px] text-brand">AI</span>
                  )}
                  {chat.device_name} · {new Date(chat.created_at * 1000).toLocaleTimeString("zh-CN", { hour12: false })}
                  {chat.role === "user" && <User className="h-3 w-3" />}
                </div>
                <div
                  className={cn(
                    "rounded-[1.35rem] px-5 py-3.5 text-sm leading-relaxed",
                    chat.role === "assistant"
                      ? "border border-white/10 bg-white/5 text-slate-200 backdrop-blur-md"
                      : "bg-brand text-white shadow-xl shadow-brand/25",
                  )}
                >
                  {chat.content}
                </div>
              </div>
            ))}
          </div>
        )}
      </div>

      <div className="glass-card h-80 rounded-[2rem] border border-white/5 p-8">
        <div className="mb-6 flex items-center justify-between">
          <h4 className="text-xs font-bold uppercase tracking-widest text-slate-500">对话活跃度</h4>
          <span className="rounded-full bg-emerald-500/10 px-3 py-1 text-xs font-bold text-emerald-400">Recharts</span>
        </div>
        <ResponsiveContainer width="100%" height="85%">
          <AreaChart data={chartData}>
            <defs>
              <linearGradient id="aiwArea" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%" stopColor="#3B82F6" stopOpacity={0.35} />
                <stop offset="95%" stopColor="#3B82F6" stopOpacity={0} />
              </linearGradient>
            </defs>
            <CartesianGrid strokeDasharray="3 3" stroke="#ffffff10" />
            <XAxis dataKey="name" stroke="#64748b" tick={{ fontSize: 11 }} />
            <YAxis stroke="#64748b" tick={{ fontSize: 11 }} allowDecimals={false} />
            <Tooltip
              contentStyle={{
                backgroundColor: "#121217",
                border: "1px solid rgba(255,255,255,0.08)",
                borderRadius: "12px",
              }}
              labelStyle={{ color: "#94a3b8" }}
            />
            <Area type="monotone" dataKey="value" stroke="#3B82F6" strokeWidth={2} fillOpacity={1} fill="url(#aiwArea)" />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
}

function escapeRe(s: string) {
  return s.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function Highlighter({ text, highlights }: { text: string; highlights: string[] }) {
  const safe = highlights.filter((h) => h.trim().length > 1);
  if (!safe.length) return <p className="leading-relaxed text-slate-400">{text}</p>;
  const pattern = new RegExp(`(${safe.map(escapeRe).join("|")})`, "gi");
  const parts = text.split(pattern);
  return (
    <p className="leading-relaxed text-slate-400">
      {parts.map((part, i) =>
        safe.some((h) => h.toLowerCase() === part.toLowerCase()) ? (
          <mark key={i} className="rounded bg-brand/25 px-0.5 font-semibold text-brand underline decoration-brand/40 underline-offset-2">
            {part}
          </mark>
        ) : (
          <span key={i}>{part}</span>
        ),
      )}
    </p>
  );
}

function parseSummary(json: string): SummaryPayload | null {
  if (!json?.trim()) return null;
  try {
    return JSON.parse(json) as SummaryPayload;
  } catch {
    return null;
  }
}

function catLabel(c: string) {
  if (c === "work_plan") return "工作计划";
  if (c === "voice_review") return "语音复盘";
  return "其他";
}

function RecordingVault({ promptSlugs }: { promptSlugs: { slug: string; title: string }[] }) {
  const [search, setSearch] = useState("");
  const [rows, setRows] = useState<Recording[]>([]);
  const [sel, setSel] = useState<Recording | null>(null);
  const [slug, setSlug] = useState("");
  const [sumBusy, setSumBusy] = useState(false);

  const load = useCallback(async () => {
    try {
      const r = await api.recordings(120);
      setRows(r.items || []);
    } catch {
      setRows([]);
    }
  }, []);
  useEffect(() => {
    void load();
  }, [load]);

  useEffect(() => {
    if (promptSlugs.length && !slug) setSlug(promptSlugs[0].slug);
  }, [promptSlugs, slug]);

  const filtered = useMemo(() => {
    const q = search.toLowerCase();
    return rows.filter(
      (r) =>
        String(r.id).includes(q) ||
        (r.transcript || "").toLowerCase().includes(q) ||
        (r.device_name || "").toLowerCase().includes(q),
    );
  }, [rows, search]);

  const cols = useMemo(() => {
    const w = filtered.filter((r) => (r.category || "other") === "work_plan");
    const v = filtered.filter((r) => r.category === "voice_review");
  const o = filtered.filter((r) => {
    const c = r.category || "other";
    return c !== "work_plan" && c !== "voice_review";
  });
    return { w, v, o };
  }, [filtered]);

  const refreshSel = async () => {
    await load();
    if (sel) {
      const r = (await api.recordings(200)).items?.find((x) => x.id === sel.id);
      if (r) setSel(r);
    }
  };

  const summarize = async () => {
    if (!sel) return;
    setSumBusy(true);
    try {
      await api.summarizeRecording(sel.id, slug || undefined);
      await refreshSel();
    } catch (e) {
      alert((e as Error).message);
    } finally {
      setSumBusy(false);
    }
  };

  const Card = ({ r }: { r: Recording }) => {
    const sum = parseSummary(r.summary_json || "");
    return (
      <motion.button
        type="button"
        layout
        onClick={() => setSel(r)}
        className="glass-card w-full rounded-[2rem] border border-white/5 p-8 text-left transition-transform hover:-translate-y-0.5"
      >
        <div className="mb-6 flex items-center justify-between">
          <div className="flex items-center gap-4">
            <div className="flex h-12 w-12 items-center justify-center rounded-2xl border border-white/10 bg-white/5 text-emerald-400">
              <Mic className="h-6 w-6" />
            </div>
            <div>
              <p className="text-sm font-black uppercase tracking-widest text-white">#{r.id}</p>
              <p className="font-mono text-[10px] uppercase tracking-widest text-slate-500">
                {r.device_name || "—"} · {new Date(r.created_at * 1000).toLocaleString("zh-CN", { hour12: false })}
              </p>
            </div>
          </div>
          <Maximize2 className="h-5 w-5 text-slate-500" />
        </div>
        <p className="mb-6 line-clamp-3 text-base font-medium leading-relaxed text-slate-300">
          {(r.transcript || "").slice(0, 180) || "（转写处理中…）"}
        </p>
        <div className="flex flex-wrap items-center gap-2 border-t border-white/5 pt-6">
          <a
            href={recordingFileUrl(r.id, "wav")}
            target="_blank"
            rel="noreferrer"
            onClick={(e) => e.stopPropagation()}
            className="rounded-lg border border-white/10 bg-white/5 px-3 py-1.5 text-[10px] font-black text-slate-300 hover:border-brand/40 hover:text-brand"
          >
            WAV
          </a>
          <a
            href={recordingFileUrl(r.id, "mp3")}
            target="_blank"
            rel="noreferrer"
            onClick={(e) => e.stopPropagation()}
            className="rounded-lg border border-white/10 bg-white/5 px-3 py-1.5 text-[10px] font-black text-slate-300 hover:border-brand/40 hover:text-brand"
          >
            MP3
          </a>
          <a
            href={recordingFileUrl(r.id, "txt")}
            target="_blank"
            rel="noreferrer"
            onClick={(e) => e.stopPropagation()}
            className="rounded-lg border border-white/10 bg-white/5 px-3 py-1.5 text-[10px] font-black text-slate-300 hover:border-brand/40 hover:text-brand"
          >
            TXT
          </a>
          {sum?.title && (
            <span className="ml-auto rounded-full border border-emerald-500/25 bg-emerald-500/10 px-3 py-1 text-[10px] font-bold uppercase tracking-widest text-emerald-400">
              已总结
            </span>
          )}
        </div>
      </motion.button>
    );
  };

  const Col = ({ title, list }: { title: string; list: Recording[] }) => (
    <div>
      <p className="mb-3 text-xs font-black uppercase tracking-[0.2em] text-brand">{title}</p>
      <div className="space-y-4">
        {list.map((r) => (
          <Card key={r.id} r={r} />
        ))}
        {!list.length && <div className="rounded-2xl border border-dashed border-white/10 py-12 text-center text-sm text-slate-600">暂无</div>}
      </div>
    </div>
  );

  const drawerSummary = sel ? parseSummary(sel.summary_json || "") : null;

  return (
    <div className="space-y-10">
      <div className="flex flex-col gap-4 sm:flex-row sm:items-center sm:justify-between">
        <div className="flex items-center gap-3">
          <div className="flex h-11 w-11 items-center justify-center rounded-2xl bg-emerald-500/15 text-emerald-400">
            <Mic className="h-6 w-6" />
          </div>
          <div>
            <h2 className="text-2xl font-black tracking-tight text-white">音频金库</h2>
            <p className="text-sm text-slate-500">三栏分类 · 右侧抽屉详情</p>
          </div>
        </div>
        <div className="relative w-full sm:max-w-sm">
          <Search className="pointer-events-none absolute left-4 top-1/2 h-4 w-4 -translate-y-1/2 text-slate-500" />
          <input
            value={search}
            onChange={(e) => setSearch(e.target.value)}
            className="w-full rounded-2xl border border-white/10 bg-white/5 py-3 pl-11 pr-4 text-sm outline-none focus:border-emerald-500/40"
            placeholder="搜索 ID / 设备 / 转写…"
          />
        </div>
      </div>

      <div className="grid gap-8 lg:grid-cols-3">
        <Col title="工作计划" list={cols.w} />
        <Col title="语音复盘" list={cols.v} />
        <Col title="其他" list={cols.o} />
      </div>

      <AnimatePresence>
        {sel && (
          <div className="fixed inset-0 z-[200] flex justify-end">
            <motion.button
              type="button"
              aria-label="关闭抽屉"
              initial={{ opacity: 0 }}
              animate={{ opacity: 1 }}
              exit={{ opacity: 0 }}
              className="absolute inset-0 border-0 bg-black/70 backdrop-blur-sm"
              onClick={() => setSel(null)}
            />
            <motion.aside
              initial={{ x: "100%" }}
              animate={{ x: 0 }}
              exit={{ x: "100%" }}
              transition={{ type: "spring", damping: 28, stiffness: 260 }}
              className="relative flex h-full w-full max-w-xl flex-col border-l border-white/10 glass-dark sm:rounded-l-[2.5rem]"
            >
              <div className="flex items-start justify-between border-b border-white/5 p-8">
                <div>
                  <p className="mb-1 text-[10px] font-black uppercase tracking-[0.25em] text-brand">Recording</p>
                  <h2 className="text-3xl font-black tracking-tighter text-white">#{sel.id}</h2>
                  <p className="mt-1 text-xs text-slate-500">{catLabel(sel.category || "other")}</p>
                </div>
                <button type="button" onClick={() => setSel(null)} className="rounded-2xl border border-white/10 bg-white/5 p-3 text-slate-400 hover:text-white">
                  <X className="h-6 w-6" />
                </button>
              </div>

              <div className="no-scrollbar flex-1 space-y-10 overflow-y-auto p-8">
                <section>
                  <h5 className="mb-3 flex items-center gap-2 text-[10px] font-bold uppercase tracking-widest text-slate-500">
                    <FileText className="h-3 w-3" /> 转写
                  </h5>
                  <div className="rounded-3xl border border-white/10 bg-white/5 p-6 text-sm leading-relaxed text-slate-300">
                    {sel.transcript || "（处理中或未生成）"}
                  </div>
                </section>

                <section className="space-y-4">
                  <h5 className="flex items-center gap-2 text-[10px] font-bold uppercase tracking-widest text-slate-500">
                    <Sparkles className="h-3 w-3 text-brand" /> AI 总结与高光
                  </h5>
                  {drawerSummary?.title ? (
                    <div className="space-y-4 rounded-3xl border border-brand/25 bg-brand/5 p-6">
                      <h6 className="text-lg font-bold text-brand">{drawerSummary.title}</h6>
                      <Highlighter text={drawerSummary.report || ""} highlights={drawerSummary.highlights || []} />
                      <div className="flex flex-wrap gap-2">
                        {(drawerSummary.highlights || []).map((t) => (
                          <span key={t} className="rounded-xl border border-white/10 bg-white/5 px-3 py-1 text-xs font-semibold text-slate-400">
                            #{t}
                          </span>
                        ))}
                      </div>
                    </div>
                  ) : (
                    <div className="flex flex-col items-center gap-4 rounded-3xl border border-white/10 bg-white/[0.03] p-10 text-center">
                      <RefreshCcw className="h-8 w-8 text-slate-600" />
                      <p className="text-sm text-slate-500">尚未生成总结</p>
                    </div>
                  )}
                  <div className="flex flex-col gap-2 sm:flex-row sm:items-center">
                    <select
                      value={slug}
                      onChange={(e) => setSlug(e.target.value)}
                      className="flex-1 rounded-xl border border-white/10 bg-[#0a0a0f] px-3 py-2.5 text-sm outline-none focus:border-brand/40"
                    >
                      {promptSlugs.map((p) => (
                        <option key={p.slug} value={p.slug}>
                          {p.title} ({p.slug})
                        </option>
                      ))}
                    </select>
                    <button
                      type="button"
                      disabled={sumBusy}
                      onClick={() => void summarize()}
                      className="rounded-xl bg-brand px-5 py-2.5 text-sm font-bold text-white shadow-lg shadow-brand/25 disabled:opacity-50"
                    >
                      {sumBusy ? "生成中…" : "生成总结"}
                    </button>
                  </div>
                </section>

                <section>
                  <h5 className="mb-3 flex items-center gap-2 text-[10px] font-bold uppercase tracking-widest text-slate-500">
                    <Clock className="h-3 w-3" /> 元数据
                  </h5>
                  <div className="grid grid-cols-2 gap-3">
                    <Meta k="设备" v={sel.device_name || "—"} />
                    <Meta k="时间" v={new Date(sel.created_at * 1000).toLocaleString("zh-CN", { hour12: false })} />
                  </div>
                </section>
              </div>

              <div className="flex gap-3 border-t border-white/5 bg-white/[0.02] p-6">
                <a
                  href={recordingFileUrl(sel.id, "wav")}
                  target="_blank"
                  rel="noreferrer"
                  className="flex flex-1 items-center justify-center gap-2 rounded-2xl border border-white/10 py-3 text-sm font-bold text-slate-200 hover:border-brand/30"
                >
                  <FileDown className="h-4 w-4" /> WAV
                </a>
                <a
                  href={recordingFileUrl(sel.id, "txt")}
                  target="_blank"
                  rel="noreferrer"
                  className="flex flex-1 items-center justify-center gap-2 rounded-2xl bg-white py-3 text-sm font-black text-black hover:scale-[1.01]"
                >
                  TXT
                </a>
              </div>
            </motion.aside>
          </div>
        )}
      </AnimatePresence>
    </div>
  );
}

function Meta({ k, v }: { k: string; v: string }) {
  return (
    <div className="rounded-2xl border border-white/10 bg-white/5 p-4">
      <div className="mb-1 text-[9px] font-black uppercase tracking-widest text-slate-600">{k}</div>
      <div className="text-sm font-semibold text-slate-300">{v}</div>
    </div>
  );
}

export default function App() {
  const { health, bad, refresh } = useHealthPoll();
  const [tab, setTab] = useState<TabId>("overview");
  const [promptOptions, setPromptOptions] = useState<{ slug: string; title: string }[]>([]);

  const loadPromptOptions = useCallback(async () => {
    try {
      const r = await api.promptCards();
      setPromptOptions((r.items || []).map((p) => ({ slug: p.slug, title: p.title })));
    } catch {
      setPromptOptions([]);
    }
  }, []);

  useEffect(() => {
    void loadPromptOptions();
  }, [loadPromptOptions]);

  return (
    <div className="min-h-screen overflow-x-hidden bg-[#0A0A0C] text-slate-200 selection:bg-brand/30 selection:text-white">
      <BackgroundOrbs />
      <GlassHeader health={health} bad={bad} />

      <main className="relative z-10 mx-auto flex min-h-[calc(100vh-12rem)] max-w-7xl flex-col px-6 py-10">
        <div className="mb-10 flex flex-col items-start justify-between gap-6 md:flex-row md:items-center">
          <div>
            <motion.h1
              initial={{ opacity: 0, y: -12 }}
              animate={{ opacity: 1, y: 0 }}
              className="glow-text mb-2 text-4xl font-black tracking-tighter text-white"
            >
              Ai Watch <span className="font-light text-white/35">服务台</span>
            </motion.h1>
            <p className="font-medium tracking-wide text-slate-500">极致暗色 · 玻璃拟态 · 与 FastAPI 后端实时联动</p>
          </div>
          <DeckTabs active={tab} onChange={setTab} />
        </div>

        <section className="flex-1">
          <AnimatePresence mode="wait">
            {tab === "overview" && (
              <motion.div
                key="overview"
                initial={{ opacity: 0, y: 14 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -10 }}
                transition={{ duration: 0.28 }}
              >
                <OverviewPanel />
              </motion.div>
            )}
            {tab === "prompts" && (
              <motion.div
                key="prompts"
                initial={{ opacity: 0, y: 14 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -10 }}
                transition={{ duration: 0.28 }}
              >
                <PromptStudio onCardsChange={loadPromptOptions} />
              </motion.div>
            )}
            {tab === "chat" && (
              <motion.div
                key="chat"
                initial={{ opacity: 0, y: 14 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -10 }}
                transition={{ duration: 0.28 }}
              >
                <ChatArchive />
              </motion.div>
            )}
            {tab === "recordings" && (
              <motion.div
                key="recordings"
                initial={{ opacity: 0, y: 14 }}
                animate={{ opacity: 1, y: 0 }}
                exit={{ opacity: 0, y: -10 }}
                transition={{ duration: 0.28 }}
              >
                <RecordingVault promptSlugs={promptOptions} />
              </motion.div>
            )}
          </AnimatePresence>
        </section>
      </main>

      <footer className="relative z-10 border-t border-white/5 py-10 text-center text-xs text-slate-600">
        <button type="button" onClick={() => void refresh()} className="mb-2 text-[10px] font-bold uppercase tracking-widest text-slate-500 hover:text-brand">
          刷新服务状态
        </button>
        <p>Ai Watch · React 服务台</p>
      </footer>
    </div>
  );
}
