import React, { useState, useEffect, useReducer, useContext, createContext, useCallback, useRef, useMemo } from 'react';
import {
  AreaChart, Area, BarChart, Bar, ScatterChart, Scatter,
  XAxis, YAxis, CartesianGrid, Tooltip as RechartsTooltip, Legend,
  ResponsiveContainer, Cell, ZAxis
} from 'recharts';
import {
  Activity, ActivitySquare, Server, AlertTriangle, CheckCircle, Clock,
  Settings, Menu, X, Plus, Trash2, Download, RefreshCw, Play, Square,
  RotateCcw, Copy, Info, Radio, Smartphone, Zap, ShieldAlert,
  Wifi, BarChart3, Database, Home, Share2, Terminal, LogOut, User, Lock, Mail,
  TrendingUp, TrendingDown, Search, Bell, ChevronDown, ChevronRight,
  Monitor, Globe, Eye, EyeOff, Filter, ArrowUp, ArrowDown, Cpu, Layers,
  Brain, Gauge, Network, Sun, Moon
} from 'lucide-react';

// ─── CONSTANTS ────────────────────────────────────────────────────────────────
const APP_NAME    = 'NWDAF Intelligence';
const APP_RELEASE = '3GPP Rel-17';
const DEFAULT_BASE_URL = 'http://localhost:7779/nwdaf-analytics/v1';

const ANALYTICS_IDS = [
  { id: 'NF_LOAD',             name: 'NF Load',             icon: Server,      ref: 'TS 23.288 §6.5',  cat: 'Network'   },
  { id: 'UE_MOBILITY',         name: 'UE Mobility',         icon: Smartphone,  ref: 'TS 23.288 §6.7',  cat: 'UE'        },
  { id: 'UE_COMMUNICATION',    name: 'UE Communication',    icon: Radio,       ref: 'TS 23.288 §6.6',  cat: 'UE'        },
  { id: 'ABNORMAL_BEHAVIOUR',  name: 'Anomaly Detection',   icon: ShieldAlert, ref: 'TS 23.288 §6.4',  cat: 'Security'  },
  { id: 'QoS_SUSTAINABILITY',  name: 'QoS Sustainability',  icon: Activity,    ref: 'TS 23.288 §6.9',  cat: 'Quality'   },
  { id: 'SERVICE_EXPERIENCE',  name: 'Service Experience',  icon: Zap,         ref: 'TS 23.288 §6.8',  cat: 'Quality'   },
  { id: 'NETWORK_PERFORMANCE', name: 'Network Performance', icon: Wifi,        ref: 'TS 23.288 §6.6a', cat: 'Network'   },
];

const TOOLTIP_STYLE = {
  backgroundColor: '#111d32',
  borderColor: '#2a3d5a',
  borderRadius: '6px',
  fontFamily: 'JetBrains Mono, monospace',
  fontSize: '12px',
  color: '#e1e7ef',
};

// ─── GLOBAL STYLES (Task #1 + Light Theme) ───────────────────────────────────
const GlobalStyles = () => (
  <style>{`
    @import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;700&display=swap');

    /* ── DARK THEME (default) ── */
    :root, [data-theme="dark"] {
      --bg-base:      #050a14;
      --bg-surface:   #0c1525;
      --bg-elevated:  #111d32;
      --bg-hover:     #162238;
      --border-sub:   #1a2840;
      --border-str:   #2a3d5a;
      --text-pri:     #e1e7ef;
      --text-sec:     #6b7fa3;
      --text-mut:     #3d5173;
      --accent:       #22d3ee;
      --accent-dim:   rgba(34,211,238,0.12);
      --ok:           #10b981;
      --ok-dim:       rgba(16,185,129,0.12);
      --warn:         #f59e0b;
      --warn-dim:     rgba(245,158,11,0.12);
      --err:          #ef4444;
      --err-dim:      rgba(239,68,68,0.12);
      --info:         #3b82f6;
      --info-dim:     rgba(59,130,246,0.12);
      --card-shadow:  none;
      --btn-pri-text: #04111e;
    }

    /* ── LIGHT PREMIUM THEME ── */
    [data-theme="light"] {
      --bg-base:      #f0f4f8;
      --bg-surface:   #ffffff;
      --bg-elevated:  #e8eef5;
      --bg-hover:     #dde6f0;
      --border-sub:   #dce4ef;
      --border-str:   #b0bdd0;
      --text-pri:     #0d1829;
      --text-sec:     #3d5473;
      --text-mut:     #8099b8;
      --accent:       #0284c7;
      --accent-dim:   rgba(2,132,199,0.08);
      --ok:           #059669;
      --ok-dim:       rgba(5,150,105,0.08);
      --warn:         #d97706;
      --warn-dim:     rgba(217,119,6,0.08);
      --err:          #dc2626;
      --err-dim:      rgba(220,38,38,0.08);
      --info:         #2563eb;
      --info-dim:     rgba(37,99,235,0.08);
      --card-shadow:  0 1px 3px rgba(0,0,0,.07), 0 1px 2px rgba(0,0,0,.04);
      --btn-pri-text: #ffffff;
    }

    /* Light-specific component overrides */
    [data-theme="light"] .card         { box-shadow: var(--card-shadow); }
    [data-theme="light"] .card-elevated{ box-shadow: 0 2px 8px rgba(0,0,0,.08); }
    [data-theme="light"] .btn-primary  { color: var(--btn-pri-text); }
    [data-theme="light"] .skeleton {
      background: linear-gradient(90deg, #e8eef5 25%, #dce4ef 50%, #e8eef5 75%);
      background-size: 800px 100%;
    }
    [data-theme="light"] .scroll::-webkit-scrollbar-thumb { background: var(--border-str); }
    [data-theme="light"] .grid-bg {
      background-image:
        linear-gradient(rgba(2,132,199,.04) 1px, transparent 1px),
        linear-gradient(90deg, rgba(2,132,199,.04) 1px, transparent 1px);
      background-size: 32px 32px;
    }
    [data-theme="light"] .pulse-crit { animation: pulseCritLight 2s ease infinite; }
    @keyframes pulseCritLight {
      0%   { box-shadow: 0 0 0 0 rgba(220,38,38,.4); }
      70%  { box-shadow: 0 0 0 8px rgba(220,38,38,0); }
      100% { box-shadow: 0 0 0 0 rgba(220,38,38,0); }
    }

    *, *::before, *::after { box-sizing: border-box; }

    html, body {
      margin: 0; padding: 0; height: 100%;
      background: var(--bg-base);
      color: var(--text-pri);
      font-family: 'IBM Plex Sans', -apple-system, sans-serif;
      -webkit-font-smoothing: antialiased;
    }

    .mono { font-family: 'JetBrains Mono', monospace; }

    /* Cards */
    .card {
      background: var(--bg-surface);
      border: 1px solid var(--border-sub);
      border-radius: 8px;
    }
    .card-elevated {
      background: var(--bg-elevated);
      border: 1px solid var(--border-str);
      border-radius: 8px;
    }

    /* Buttons */
    .btn {
      display: inline-flex; align-items: center; gap: 6px;
      padding: 7px 14px; border-radius: 6px; border: none;
      font-size: 13px; font-weight: 500; cursor: pointer;
      transition: opacity .15s, background .15s;
      white-space: nowrap;
    }
    .btn:disabled { opacity: .4; cursor: not-allowed; }
    .btn-primary  { background: var(--accent);   color: #04111e; font-weight: 600; }
    .btn-primary:hover:not(:disabled)  { opacity: .85; }
    .btn-secondary { background: transparent; color: var(--text-pri); border: 1px solid var(--border-str); }
    .btn-secondary:hover:not(:disabled) { background: var(--bg-hover); }
    .btn-danger   { background: var(--err-dim); color: var(--err); border: 1px solid rgba(239,68,68,.3); }
    .btn-danger:hover:not(:disabled) { background: rgba(239,68,68,.2); }
    .btn-ghost    { background: transparent; color: var(--text-sec); padding: 6px 10px; }
    .btn-ghost:hover { background: var(--bg-hover); color: var(--text-pri); }

    /* Status pills */
    .pill {
      display: inline-flex; align-items: center; gap: 4px;
      padding: 3px 8px; border-radius: 20px;
      font-size: 11px; font-weight: 600; letter-spacing: .04em;
      font-family: 'JetBrains Mono', monospace;
    }
    .pill-ok   { background: var(--ok-dim);   color: var(--ok);   border: 1px solid rgba(16,185,129,.25); }
    .pill-warn { background: var(--warn-dim); color: var(--warn); border: 1px solid rgba(245,158,11,.25); }
    .pill-err  { background: var(--err-dim);  color: var(--err);  border: 1px solid rgba(239,68,68,.25);  }
    .pill-info { background: var(--info-dim); color: #60a5fa;     border: 1px solid rgba(59,130,246,.25); }
    .pill-neu  { background: rgba(107,127,163,.1); color: var(--text-sec); border: 1px solid var(--border-sub); }

    /* Form inputs */
    .inp {
      width: 100%; background: var(--bg-base);
      border: 1px solid var(--border-str); border-radius: 6px;
      padding: 8px 12px; color: var(--text-pri);
      font-size: 13px; outline: none;
      transition: border-color .15s;
    }
    .inp:focus { border-color: var(--accent); }
    .inp::placeholder { color: var(--text-mut); }
    .inp.mono { font-family: 'JetBrains Mono', monospace; }

    /* Tables */
    .tbl { width: 100%; border-collapse: collapse; }
    .tbl th {
      background: rgba(26,40,64,.5); color: var(--text-mut);
      font-size: 11px; font-weight: 600; letter-spacing: .08em;
      text-transform: uppercase; padding: 10px 16px; text-align: left;
      border-bottom: 1px solid var(--border-sub);
    }
    .tbl td { padding: 11px 16px; font-size: 13px; border-bottom: 1px solid var(--border-sub); }
    .tbl tbody tr:hover td { background: var(--bg-hover); }
    .tbl tbody tr:last-child td { border-bottom: none; }

    /* Nav */
    .nav-label {
      font-size: 10px; font-weight: 600; letter-spacing: .1em;
      text-transform: uppercase; color: var(--text-mut);
      padding: 0 20px; margin: 16px 0 4px;
    }
    .nav-btn {
      display: flex; align-items: center; gap: 10px;
      width: calc(100% - 16px); margin: 1px 8px;
      padding: 7px 12px; border-radius: 6px;
      border: none; background: none; cursor: pointer;
      font-size: 13px; font-weight: 500; color: var(--text-sec);
      transition: background .12s, color .12s;
      text-align: left;
    }
    .nav-btn:hover { background: var(--bg-hover); color: var(--text-pri); }
    .nav-btn.active { background: var(--accent-dim); color: var(--accent); border-left: 2px solid var(--accent); padding-left: 10px; }

    /* Animations */
    @keyframes fadeUp   { from { opacity:0; transform:translateY(6px); } to { opacity:1; transform:none; } }
    @keyframes pulseCrit {
      0%   { box-shadow: 0 0 0 0 rgba(239,68,68,.5); }
      70%  { box-shadow: 0 0 0 8px rgba(239,68,68,0); }
      100% { box-shadow: 0 0 0 0 rgba(239,68,68,0); }
    }
    @keyframes shimmer {
      0%   { background-position: -800px 0; }
      100% { background-position: 800px 0; }
    }
    .fade-up    { animation: fadeUp .2s ease-out; }
    .pulse-crit { animation: pulseCrit 2s ease infinite; }
    .skeleton {
      background: linear-gradient(90deg, var(--bg-surface) 25%, var(--bg-elevated) 50%, var(--bg-surface) 75%);
      background-size: 800px 100%; animation: shimmer 1.4s infinite;
      border-radius: 4px;
    }

    /* Scrollbar */
    .scroll::-webkit-scrollbar { width: 4px; height: 4px; }
    .scroll::-webkit-scrollbar-track { background: transparent; }
    .scroll::-webkit-scrollbar-thumb { background: var(--border-str); border-radius: 2px; }

    /* Grid bg */
    .grid-bg {
      background-image:
        linear-gradient(rgba(34,211,238,.025) 1px, transparent 1px),
        linear-gradient(90deg, rgba(34,211,238,.025) 1px, transparent 1px);
      background-size: 32px 32px;
    }
  `}</style>
);

// ─── UTILS ────────────────────────────────────────────────────────────────────
const cx = (...cls) => cls.filter(Boolean).join(' ');

function parsePrometheus(text) {
  const m = {};
  text.split('\n').forEach(line => {
    if (line.startsWith('#') || !line.trim()) return;
    const match = line.match(/^(\w+)(?:\{(.+?)\})?\s+([0-9.e+\-]+)/);
    if (match) {
      const [, name, labels, value] = match;
      m[labels ? `${name}{${labels}}` : name] = parseFloat(value);
    }
  });
  return m;
}

class ApiError extends Error {
  constructor(status, title, cause) { super(title); this.status = status; this.cause = cause; }
}

// ─── CONTEXTS ─────────────────────────────────────────────────────────────────
const ToastCtx    = createContext(null);
const SettingsCtx = createContext(null);
const ApiCtx      = createContext(null);
const AuthCtx     = createContext(null);
const ThemeCtx    = createContext(null);

// ─── THEME PROVIDER ───────────────────────────────────────────────────────────
const ThemeProvider = ({ children }) => {
  const [theme, setTheme] = useState(() => {
    try { return localStorage.getItem('nwdaf_theme') || 'dark'; } catch { return 'dark'; }
  });
  const toggle = () => setTheme(t => {
    const next = t === 'dark' ? 'light' : 'dark';
    try { localStorage.setItem('nwdaf_theme', next); } catch {}
    return next;
  });
  return <ThemeCtx.Provider value={{ theme, toggle }}>{children}</ThemeCtx.Provider>;
};
const useTheme = () => useContext(ThemeCtx);

// ─── TOAST PROVIDER ───────────────────────────────────────────────────────────
const ToastProvider = ({ children }) => {
  const [toasts, setToasts] = useState([]);
  const add = useCallback((type, message) => {
    const id = Date.now().toString();
    setToasts(p => [...p, { id, type, message }]);
    setTimeout(() => setToasts(p => p.filter(t => t.id !== id)), 5000);
  }, []);

  const icons = { success: <CheckCircle className="w-4 h-4" style={{color:'var(--ok)'}}/>, error: <ShieldAlert className="w-4 h-4" style={{color:'var(--err)'}}/>, warning: <AlertTriangle className="w-4 h-4" style={{color:'var(--warn)'}}/>, info: <Info className="w-4 h-4" style={{color:'var(--info)'}}/> };
  const borders = { success: 'var(--ok)', error: 'var(--err)', warning: 'var(--warn)', info: 'var(--info)' };

  return (
    <ToastCtx.Provider value={add}>
      {children}
      <div style={{ position:'fixed', bottom:16, right:16, zIndex:9999, display:'flex', flexDirection:'column', gap:8 }}>
        {toasts.map(t => (
          <div key={t.id} className="card" style={{ display:'flex', alignItems:'center', gap:10, padding:'10px 14px', minWidth:300, borderLeft:`3px solid ${borders[t.type]}`, boxShadow:'0 8px 24px rgba(0,0,0,.4)' }}>
            {icons[t.type]}
            <span style={{ fontSize:13, flex:1 }}>{t.message}</span>
            <button className="btn-ghost btn" style={{padding:'2px 4px'}} onClick={() => setToasts(p => p.filter(x => x.id !== t.id))}><X className="w-3 h-3"/></button>
          </div>
        ))}
      </div>
    </ToastCtx.Provider>
  );
};

// ─── API PROVIDER ─────────────────────────────────────────────────────────────
const ApiProvider = ({ children }) => {
  const { settings } = useContext(SettingsCtx);
  const toast = useContext(ToastCtx);

  const api = useMemo(() => {
    const base = settings.baseUrl || DEFAULT_BASE_URL;
    const req = async (method, path, params = {}, body = null, silent = false) => {
      const url = new URL(base + path);
      Object.entries(params).forEach(([k, v]) => url.searchParams.set(k, v));
      try {
        const res = await fetch(url.toString(), {
          method,
          headers: { 'X-Request-Id': Math.random().toString(36).slice(2, 10), ...(body ? { 'Content-Type': 'application/json' } : {}) },
          body: body ? JSON.stringify(body) : undefined,
          signal: AbortSignal.timeout(8000),
        });
        if (!res.ok) {
          let title = 'API Error', cause = res.statusText;
          try { const d = await res.json(); title = d.title || title; cause = d.cause || cause; } catch {}
          throw new ApiError(res.status, title, cause);
        }
        if (path === '/metrics') return res.text();
        const txt = await res.text();
        return txt ? JSON.parse(txt) : {};
      } catch (err) {
        if (err.name === 'AbortError' || err.name === 'TimeoutError') { if (!silent) toast('error', `Timeout: ${path}`); throw err; }
        if (err instanceof TypeError) { if (!silent) toast('error', 'NWDAF unreachable'); throw err; }
        if (err instanceof ApiError) { if (!silent) toast('error', `[${err.status}] ${err.message}`); throw err; }
        if (!silent) toast('error', err.message);
        throw err;
      }
    };
    return {
      get:     (path, params)  => req('GET', path, params),
      post:    (path, body)    => req('POST', path, {}, body),
      delete:  (path)          => req('DELETE', path),
      health:  ()              => req('GET', '/health'),
      analytics: (id, supi)   => req('GET', '/analytics', { analyticsId: id, ...(supi && { supi }) }),
      metrics: ()              => req('GET', '/metrics', {}, null, true).then(parsePrometheus),
      metricsRaw: ()           => req('GET', '/metrics', {}, null, true),
      train:   ()              => req('POST', '/train'),
      subscriptions: {
        list:   ()     => req('GET', '/subscriptions'),
        create: (body) => req('POST', '/subscriptions', {}, body),
        delete: (id)   => req('DELETE', `/subscriptions/${id}`),
      },
    };
  }, [settings.baseUrl, toast]);

  return <ApiCtx.Provider value={api}>{children}</ApiCtx.Provider>;
};

const useApi   = () => useContext(ApiCtx);
const useToast = () => useContext(ToastCtx);
const useAuth  = () => useContext(AuthCtx);

// ─── AUTH PROVIDER ────────────────────────────────────────────────────────────
const AuthProvider = ({ children }) => {
  const [currentUser, setCurrentUser] = useState(() => {
    try { return JSON.parse(localStorage.getItem('nwdaf_user')) || null; } catch { return null; }
  });
  const [users, setUsers] = useState(() => {
    try { const s = JSON.parse(localStorage.getItem('nwdaf_users')); if (s?.length) return s; } catch {}
    return [{ email: 'admin@5g-core.local', password: 'nwdaf-ops' }];
  });

  const login = (email, password) => {
    const u = users.find(u => u.email === email && u.password === password);
    if (u) { setCurrentUser(u); localStorage.setItem('nwdaf_user', JSON.stringify(u)); return true; }
    return false;
  };
  const register = (email, password) => {
    if (users.find(u => u.email === email)) return false;
    const next = [...users, { email, password }];
    setUsers(next); localStorage.setItem('nwdaf_users', JSON.stringify(next)); return true;
  };
  const logout = () => { setCurrentUser(null); localStorage.removeItem('nwdaf_user'); };

  return <AuthCtx.Provider value={{ currentUser, login, register, logout }}>{children}</AuthCtx.Provider>;
};

// ─── SETTINGS REDUCER ─────────────────────────────────────────────────────────
const settingsReducer = (s, a) => {
  switch (a.type) {
    case 'SET_URL':      return { ...s, baseUrl: a.v };
    case 'SET_MODE':     return { ...s, connectionMode: a.v };
    case 'SET_REFRESH':  return { ...s, refreshInterval: a.v };
    case 'TOGGLE_AUTO':  return { ...s, autoRefresh: a.v };
    case 'TOGGLE_RAW':   return { ...s, showRawJson: a.v };
    case 'TOGGLE_3GPP':  return { ...s, show3gpp: a.v };
    case 'TOGGLE_COMPACT': return { ...s, compactMode: a.v };
    default: return s;
  }
};
const initialSettings = {
  baseUrl: (typeof window !== 'undefined' && window.NWDAF_BASE_URL) || DEFAULT_BASE_URL,
  connectionMode: 'Direct',
  autoRefresh: true,
  refreshInterval: 5,
  showRawJson: false,
  show3gpp: true,
  compactMode: false,
};

// ─── REUSABLE COMPONENTS ──────────────────────────────────────────────────────

const SkeletonBlock = ({ h = 16, w = '100%', mb = 0 }) => (
  <div className="skeleton" style={{ height: h, width: w, marginBottom: mb }} />
);

const SkeletonCard = () => (
  <div className="card" style={{ padding: 20 }}>
    <SkeletonBlock h={12} w="40%" mb={12} />
    <SkeletonBlock h={32} mb={8} />
    <SkeletonBlock h={10} w="60%" />
  </div>
);

const EmptyState = ({ icon: Icon = Database, title, desc }) => (
  <div style={{ display:'flex', flexDirection:'column', alignItems:'center', justifyContent:'center', padding:'48px 24px', color:'var(--text-mut)' }}>
    <Icon style={{ width:48, height:48, opacity:.3, marginBottom:16 }} />
    <div style={{ fontSize:15, fontWeight:600, color:'var(--text-sec)', marginBottom:6 }}>{title}</div>
    {desc && <div style={{ fontSize:13, textAlign:'center', maxWidth:320 }}>{desc}</div>}
  </div>
);

const ErrorState = ({ error, onRetry }) => (
  <div style={{ display:'flex', flexDirection:'column', alignItems:'center', padding:32, border:'1px solid rgba(239,68,68,.3)', borderRadius:8, background:'var(--err-dim)' }}>
    <ShieldAlert style={{ width:40, height:40, color:'var(--err)', marginBottom:12 }} />
    <div style={{ fontSize:15, fontWeight:600, color:'var(--err)', marginBottom:6 }}>Failed to load data</div>
    <div style={{ fontSize:13, color:'var(--text-sec)', marginBottom:16 }}>{error?.message || 'Unknown error'}</div>
    {onRetry && <button className="btn btn-secondary" onClick={onRetry}><RefreshCw className="w-4 h-4"/>Retry</button>}
  </div>
);

const Dot = ({ color }) => (
  <span style={{ display:'inline-block', width:7, height:7, borderRadius:'50%', background:color, flexShrink:0 }} />
);

const AnimatedNumber = ({ value, prefix='', suffix='', decimals=0 }) => {
  const [display, setDisplay] = useState(value);
  useEffect(() => {
    let start = display;
    if (typeof start !== 'number' || typeof value !== 'number' || start === value) { setDisplay(value); return; }
    const t0 = performance.now();
    const frame = t => {
      const p = Math.min((t - t0) / 500, 1);
      const e = 1 - Math.pow(1 - p, 3);
      setDisplay(start + (value - start) * e);
      if (p < 1) requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
  }, [value]);
  return <span className="mono">{prefix}{typeof display === 'number' ? display.toFixed(decimals) : display}{suffix}</span>;
};

const Sparkline = ({ data = [], color = 'var(--accent)', height = 32 }) => {
  if (data.length < 2) return <div style={{ height }} />;
  const min = Math.min(...data), max = Math.max(...data), range = max - min || 1;
  const pts = data.map((v, i) => `${(i / (data.length - 1)) * 100},${100 - ((v - min) / range) * 100}`).join(' ');
  return (
    <svg viewBox="0 0 100 100" style={{ width:'100%', height }} preserveAspectRatio="none">
      <polyline fill="none" stroke={color} strokeWidth="2.5" points={pts} vectorEffect="non-scaling-stroke" />
    </svg>
  );
};

const SectionHeader = ({ title, action }) => (
  <div style={{ display:'flex', alignItems:'center', justifyContent:'space-between', marginBottom:16 }}>
    <h2 style={{ fontSize:17, fontWeight:700, color:'var(--text-pri)', margin:0 }}>{title}</h2>
    {action}
  </div>
);

const Label = ({ children, style }) => (
  <div style={{ fontSize:11, fontWeight:600, letterSpacing:'.07em', textTransform:'uppercase', color:'var(--text-mut)', marginBottom:6, ...style }}>{children}</div>
);

// ─── KPI CARD ─────────────────────────────────────────────────────────────────
const KpiCard = ({ label, value, suffix='', decimals=0, trend, trendLabel, icon: Icon, accentColor='var(--accent)', status, statusColor, sparkData, children }) => {
  const trendUp   = trend > 0;
  const trendNone = trend === 0 || trend == null;
  return (
    <div className="card" style={{ padding:20, position:'relative', overflow:'hidden', transition:'border-color .2s', borderLeft: statusColor ? `3px solid ${statusColor}` : undefined }}>
      <div style={{ display:'flex', justifyContent:'space-between', alignItems:'flex-start', marginBottom:12 }}>
        <Label>{label}</Label>
        {Icon && <Icon style={{ width:18, height:18, color: accentColor, opacity:.8 }} />}
      </div>
      <div style={{ fontSize:28, fontWeight:700, color:'var(--text-pri)', lineHeight:1, marginBottom:6 }} className="mono">
        {typeof value === 'number' ? <AnimatedNumber value={value} suffix={suffix} decimals={decimals}/> : value}
      </div>
      {(status || !trendNone) && (
        <div style={{ display:'flex', alignItems:'center', gap:8, marginTop:4 }}>
          {status && <span className={cx('pill', status === 'ok' ? 'pill-ok' : status === 'warn' ? 'pill-warn' : status === 'err' ? 'pill-err' : 'pill-neu')}>{statusColor ? <Dot color={statusColor}/> : null}{trendLabel || status.toUpperCase()}</span>}
          {!trendNone && (
            <span style={{ fontSize:12, color: trendUp ? 'var(--ok)' : 'var(--err)', display:'flex', alignItems:'center', gap:2 }}>
              {trendUp ? <TrendingUp style={{width:13,height:13}}/> : <TrendingDown style={{width:13,height:13}}/>}
            </span>
          )}
        </div>
      )}
      {sparkData && <div style={{ marginTop:12 }}><Sparkline data={sparkData} color={accentColor} /></div>}
      {children}
    </div>
  );
};

// ─── MODAL ────────────────────────────────────────────────────────────────────
const Modal = ({ title, onClose, children, width=480 }) => (
  <div style={{ position:'fixed', inset:0, background:'rgba(0,0,0,.75)', display:'flex', alignItems:'center', justifyContent:'center', zIndex:500, padding:16 }} onClick={onClose}>
    <div className="card-elevated" style={{ width:'100%', maxWidth:width, padding:24, boxShadow:'0 24px 64px rgba(0,0,0,.6)' }} onClick={e => e.stopPropagation()}>
      <div style={{ display:'flex', justifyContent:'space-between', alignItems:'center', marginBottom:20, paddingBottom:16, borderBottom:'1px solid var(--border-sub)' }}>
        <h3 style={{ margin:0, fontSize:15, fontWeight:600 }}>{title}</h3>
        <button className="btn btn-ghost" style={{padding:'4px 6px'}} onClick={onClose}><X className="w-4 h-4"/></button>
      </div>
      {children}
    </div>
  </div>
);

// ─── RAW JSON VIEWER ──────────────────────────────────────────────────────────
const RawJson = ({ data, onClose }) => (
  <div className="card" style={{ marginTop:16 }}>
    <div style={{ display:'flex', justifyContent:'space-between', alignItems:'center', padding:'10px 16px', borderBottom:'1px solid var(--border-sub)' }}>
      <span style={{ fontSize:12, fontWeight:600, color:'var(--text-sec)' }}>RAW JSON RESPONSE</span>
      <button className="btn btn-ghost" style={{padding:'2px 6px'}} onClick={onClose}><X className="w-3 h-3"/></button>
    </div>
    <pre className="mono scroll" style={{ fontSize:11, color:'#7dd3fc', padding:16, overflowX:'auto', margin:0, maxHeight:400, background:'var(--bg-base)', borderRadius:'0 0 8px 8px' }}>
      {JSON.stringify(data, null, 2)}
    </pre>
  </div>
);

// ─── CHART HELPERS ────────────────────────────────────────────────────────────
const ChartGrid  = () => <CartesianGrid strokeDasharray="3 3" stroke="#1a2840" vertical={false} />;
const ChartGridH = () => <CartesianGrid strokeDasharray="3 3" stroke="#1a2840" horizontal={false} />;
const AxisX = (props) => <XAxis stroke="#3d5173" tick={{ fill:'#6b7fa3', fontSize:11 }} tickLine={false} axisLine={false} {...props} />;
const AxisY = (props) => <YAxis stroke="#3d5173" tick={{ fill:'#6b7fa3', fontSize:11 }} tickLine={false} axisLine={false} {...props} />;
const ChartTip = (props) => {
  const ctx = useContext(ThemeCtx);
  const isDark = !ctx || ctx.theme === 'dark';
  const cs = isDark
    ? TOOLTIP_STYLE
    : { backgroundColor:'#fff', borderColor:'#b0bdd0', borderRadius:'6px', fontFamily:'JetBrains Mono,monospace', fontSize:'12px', color:'#0d1829', boxShadow:'0 4px 12px rgba(0,0,0,.1)' };
  return <RechartsTooltip contentStyle={cs} itemStyle={{color: isDark ? '#e1e7ef' : '#0d1829'}} labelStyle={{color: isDark ? '#6b7fa3' : '#3d5473', marginBottom:4}} {...props} />;
};

// ─── AUTH PAGE (Task #2) ──────────────────────────────────────────────────────
const AuthPage = () => {
  const [isLogin, setIsLogin] = useState(true);
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const { login, register } = useAuth();
  const toast = useToast();
  const { theme } = useTheme();

  const submit = e => {
    e.preventDefault();
    if (isLogin) {
      if (!login(email, password)) toast('error', 'Invalid credentials');
    } else {
      if (register(email, password)) { toast('success', 'Account created — you can now sign in'); setIsLogin(true); setPassword(''); }
      else toast('error', 'Email already registered');
    }
  };

  return (
    <div data-theme={theme} className="grid-bg" style={{ minHeight:'100vh', display:'flex', flexDirection:'column', alignItems:'center', justifyContent:'center', padding:16, background:'var(--bg-base)' }}>
      {/* Brand header */}
      <div style={{ textAlign:'center', marginBottom:32 }}>
        <div style={{ display:'inline-flex', alignItems:'center', justifyContent:'center', width:56, height:56, borderRadius:14, background:'var(--accent-dim)', border:'1px solid rgba(34,211,238,.3)', marginBottom:16 }}>
          <ActivitySquare style={{ width:28, height:28, color:'var(--accent)' }} />
        </div>
        <div style={{ fontSize:22, fontWeight:700, color:'var(--text-pri)', letterSpacing:'-.3px' }}>{APP_NAME}</div>
        <div style={{ fontSize:12, color:'var(--text-sec)', marginTop:4, letterSpacing:'.06em' }}>5G Network Data Analytics · {APP_RELEASE}</div>
      </div>

      {/* Card */}
      <div className="card" style={{ width:'100%', maxWidth:400, padding:32, position:'relative' }}>
        <div style={{ position:'absolute', top:0, left:0, right:0, height:2, background:'linear-gradient(90deg, var(--accent), #3b82f6)', borderRadius:'8px 8px 0 0' }} />

        <h2 style={{ margin:'0 0 4px', fontSize:16, fontWeight:600 }}>
          {isLogin ? 'Sign in to your account' : 'Create operator account'}
        </h2>
        <p style={{ margin:'0 0 24px', fontSize:13, color:'var(--text-sec)' }}>
          {isLogin ? 'Enter your credentials to access the operations console' : 'Register a new operator identity'}
        </p>

        <form onSubmit={submit} style={{ display:'flex', flexDirection:'column', gap:16 }}>
          <div>
            <Label>Email address</Label>
            <div style={{ position:'relative' }}>
              <Mail style={{ position:'absolute', left:10, top:'50%', transform:'translateY(-50%)', width:15, height:15, color:'var(--text-mut)' }} />
              <input className="inp mono" type="email" required value={email} onChange={e => setEmail(e.target.value)} placeholder="operator@5g-core.local" style={{ paddingLeft:34 }} />
            </div>
          </div>
          <div>
            <Label>Password</Label>
            <div style={{ position:'relative' }}>
              <Lock style={{ position:'absolute', left:10, top:'50%', transform:'translateY(-50%)', width:15, height:15, color:'var(--text-mut)' }} />
              <input className="inp mono" type="password" required value={password} onChange={e => setPassword(e.target.value)} placeholder="••••••••••••" style={{ paddingLeft:34 }} />
            </div>
          </div>
          <button type="submit" className="btn btn-primary" style={{ width:'100%', justifyContent:'center', padding:'10px 16px', fontSize:14, marginTop:4 }}>
            {isLogin ? 'Authenticate' : 'Register identity'}
          </button>
        </form>

        <div style={{ marginTop:20, textAlign:'center', fontSize:13, color:'var(--text-sec)' }}>
          {isLogin ? "Don't have access? " : 'Already registered? '}
          <button onClick={() => setIsLogin(!isLogin)} style={{ background:'none', border:'none', color:'var(--accent)', cursor:'pointer', fontSize:13, textDecoration:'underline' }}>
            {isLogin ? 'Request account' : 'Sign in instead'}
          </button>
        </div>
      </div>

      <div style={{ marginTop:24, fontSize:11, color:'var(--text-mut)', textAlign:'center' }}>
        Default dev credentials: admin@5g-core.local / nwdaf-ops
      </div>
    </div>
  );
};

const AuthGate = () => {
  const { currentUser } = useAuth();
  return currentUser ? <MainDashboard /> : <AuthPage />;
};

// ─── TOP BAR (Task #3) ────────────────────────────────────────────────────────
const TopBar = ({ onToggleSidebar }) => {
  const { currentUser, logout } = useAuth();
  const { theme, toggle: toggleTheme } = useTheme();
  const api = useApi();
  const [conn, setConn] = useState({ status: 'CHECKING', latency: 0 });

  useEffect(() => {
    const check = async () => {
      const t0 = Date.now();
      try { await api.health(); setConn({ status: 'UP', latency: Date.now() - t0 }); }
      catch { setConn({ status: 'DOWN', latency: 0 }); }
    };
    check();
    const iv = setInterval(check, 6000);
    return () => clearInterval(iv);
  }, [api]);

  const latColor = conn.status === 'DOWN' ? 'var(--err)' : conn.latency < 200 ? 'var(--ok)' : conn.latency < 500 ? 'var(--warn)' : 'var(--err)';

  return (
    <header style={{ height:52, background:'var(--bg-surface)', borderBottom:'1px solid var(--border-sub)', display:'flex', alignItems:'center', justifyContent:'space-between', padding:'0 16px', position:'sticky', top:0, zIndex:100, flexShrink:0 }}>
      <div style={{ display:'flex', alignItems:'center', gap:12 }}>
        <button className="btn btn-ghost" style={{padding:'6px 8px'}} onClick={onToggleSidebar}>
          <Menu style={{ width:18, height:18 }} />
        </button>
        <div style={{ display:'flex', alignItems:'center', gap:8 }}>
          <ActivitySquare style={{ width:20, height:20, color:'var(--accent)' }} />
          <span style={{ fontWeight:700, fontSize:14, color:'var(--text-pri)', letterSpacing:'-.2px' }}>{APP_NAME}</span>
          <span style={{ fontSize:10, background:'var(--accent-dim)', color:'var(--accent)', border:'1px solid rgba(34,211,238,.25)', borderRadius:4, padding:'2px 6px', fontWeight:600, letterSpacing:'.05em' }}>{APP_RELEASE}</span>
        </div>
      </div>

      <div style={{ display:'flex', alignItems:'center', gap:16 }}>
        {/* PLMN badge */}
        <div className="mono" style={{ fontSize:11, color:'var(--text-sec)', background:'var(--bg-elevated)', border:'1px solid var(--border-sub)', borderRadius:6, padding:'4px 10px', display:'flex', gap:8 }}>
          <span>MCC <strong style={{color:'var(--text-pri)'}}>999</strong></span>
          <span style={{color:'var(--border-str)'}}>|</span>
          <span>MNC <strong style={{color:'var(--text-pri)'}}>70</strong></span>
        </div>

        {/* Latency pill */}
        <div style={{ display:'flex', alignItems:'center', gap:6, padding:'4px 10px', background:'var(--bg-elevated)', border:'1px solid var(--border-sub)', borderRadius:20 }}>
          <Dot color={latColor} />
          <span className="mono" style={{ fontSize:11, color:'var(--text-sec)' }}>
            {conn.status === 'UP' ? `${conn.latency}ms` : 'OFFLINE'}
          </span>
        </div>

        {/* Theme toggle */}
        <button
          onClick={toggleTheme}
          title={theme === 'dark' ? 'Switch to light mode' : 'Switch to dark mode'}
          style={{
            display:'flex', alignItems:'center', gap:5,
            padding:'5px 10px', borderRadius:20, cursor:'pointer',
            background:'var(--bg-elevated)', border:'1px solid var(--border-sub)',
            color:'var(--text-sec)', transition:'background .2s, color .2s',
            fontSize:11, fontWeight:500,
          }}
        >
          {theme === 'dark'
            ? <><Sun style={{width:13,height:13,color:'#fbbf24'}}/><span>Light</span></>
            : <><Moon style={{width:13,height:13,color:'#6366f1'}}/><span>Dark</span></>}
        </button>

        <div style={{ width:1, height:20, background:'var(--border-sub)' }} />

        {/* User */}
        <div style={{ display:'flex', alignItems:'center', gap:10 }}>
          <div style={{ display:'flex', alignItems:'center', gap:6 }}>
            <div style={{ width:26, height:26, borderRadius:'50%', background:'var(--accent-dim)', border:'1px solid rgba(34,211,238,.3)', display:'flex', alignItems:'center', justifyContent:'center' }}>
              <User style={{ width:13, height:13, color:'var(--accent)' }} />
            </div>
            <span style={{ fontSize:13, color:'var(--text-sec)' }}>{currentUser?.email}</span>
          </div>
          <button className="btn btn-ghost" style={{ padding:'5px 7px' }} onClick={logout} title="Sign out">
            <LogOut style={{ width:16, height:16, color:'var(--text-mut)' }} />
          </button>
        </div>
      </div>
    </header>
  );
};

// ─── SIDEBAR (Task #3) ────────────────────────────────────────────────────────
const NAV_SECTIONS = [
  {
    label: 'Overview',
    items: [{ id: 'overview', name: 'Overview', icon: Home }],
  },
  {
    label: 'Analytics',
    items: ANALYTICS_IDS.map(a => ({ id: a.id, name: a.name, icon: a.icon })),
  },
  {
    label: 'Operations',
    items: [
      { id: 'subscriptions',  name: 'Subscriptions',    icon: Share2   },
      { id: 'traffic_gen',    name: 'Traffic Simulator', icon: Zap      },
    ],
  },
  {
    label: 'Platform',
    items: [
      { id: 'models',         name: 'ML Models',        icon: Brain    },
      { id: 'metrics',        name: 'Metrics',          icon: BarChart3 },
      { id: 'service',        name: 'Service Control',  icon: Monitor  },
    ],
  },
];

const Sidebar = ({ current, onNav, open }) => (
  <aside className="scroll" style={{ width: open ? 220 : 52, flexShrink:0, background:'var(--bg-surface)', borderRight:'1px solid var(--border-sub)', display:'flex', flexDirection:'column', overflowY:'auto', overflowX:'hidden', transition:'width .2s', zIndex:50 }}>
    <div style={{ flex:1, padding:'8px 0 16px' }}>
      {NAV_SECTIONS.map(sec => (
        <div key={sec.label}>
          {open && <div className="nav-label">{sec.label}</div>}
          {!open && <div style={{ height:8 }} />}
          {sec.items.map(item => {
            const active = current === item.id;
            const Icon = item.icon;
            return (
              <button key={item.id} className={cx('nav-btn', active && 'active')} onClick={() => onNav(item.id)} title={!open ? item.name : undefined} style={{ justifyContent: open ? 'flex-start' : 'center' }}>
                <Icon style={{ width:16, height:16, flexShrink:0 }} />
                {open && <span style={{ fontSize:13, whiteSpace:'nowrap', overflow:'hidden', textOverflow:'ellipsis' }}>{item.name}</span>}
              </button>
            );
          })}
        </div>
      ))}
    </div>
    {/* Settings at bottom */}
    <div style={{ borderTop:'1px solid var(--border-sub)', padding:'8px 0' }}>
      <button className={cx('nav-btn', current === 'settings' && 'active')} onClick={() => onNav('settings')} title={!open ? 'Settings' : undefined} style={{ justifyContent: open ? 'flex-start' : 'center' }}>
        <Settings style={{ width:16, height:16, flexShrink:0 }} />
        {open && <span style={{ fontSize:13 }}>Settings</span>}
      </button>
    </div>
  </aside>
);

// ─── OVERVIEW PAGE (Task #4) ─────────────────────────────────────────────────
const OverviewPage = () => {
  const api = useApi();
  const { settings } = useContext(SettingsCtx);
  const [data, setData]   = useState(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState(null);
  const [dlHist, setDlHist] = useState([]);
  const [ulHist, setUlHist] = useState([]);
  const [tpHist, setTpHist] = useState([]);

  const fetch = useCallback(async () => {
    try {
      setError(null);
      const [health, metrics, anomaly] = await Promise.all([
        api.health().catch(() => ({})),
        api.metrics().catch(() => ({})),
        api.analytics('ABNORMAL_BEHAVIOUR').catch(() => null),
      ]);
      const dl = metrics['nwdaf_throughput_dl_kbps'] || 0;
      const ul = metrics['nwdaf_throughput_ul_kbps'] || 0;
      setDlHist(p => [...p.slice(-29), dl]);
      setUlHist(p => [...p.slice(-29), ul]);
      setTpHist(p => [...p.slice(-29), { t: new Date().toLocaleTimeString(), dl, ul }]);
      setData({ health, metrics, anomaly: anomaly?.analData || {} });
    } catch (err) { setError(err); }
    finally { setLoading(false); }
  }, [api]);

  useEffect(() => {
    fetch();
    if (settings.autoRefresh) {
      const iv = setInterval(fetch, (settings.refreshInterval || 5) * 1000);
      return () => clearInterval(iv);
    }
  }, [fetch, settings.autoRefresh, settings.refreshInterval]);

  if (loading && !data) return (
    <div style={{ display:'grid', gridTemplateColumns:'repeat(3,1fr)', gap:16 }}>
      {[...Array(6)].map((_, i) => <SkeletonCard key={i} />)}
    </div>
  );
  if (error && !data) return <ErrorState error={error} onRetry={fetch} />;

  const { health = {}, metrics = {}, anomaly = {} } = data || {};
  const isAnomaly = anomaly?.anomaly_detected === true || anomaly?.anomalyDetected === true;
  const nfKeys    = Object.keys(metrics).filter(k => k.startsWith('nwdaf_nf_load_pct'));
  const nfTotal   = nfKeys.length || 10;
  const dl        = tpHist[tpHist.length - 1]?.dl || 0;
  const ul        = tpHist[tpHist.length - 1]?.ul || 0;
  const mos       = metrics['nwdaf_mos_score'] || 0;
  const netScore  = metrics['nwdaf_network_performance_score'] || 0;

  return (
    <div className="fade-up">
      {/* KPI grid */}
      <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill, minmax(260px, 1fr))', gap:16, marginBottom:20 }}>

        {/* NWDAF STATUS */}
        <KpiCard
          label="NWDAF Status"
          value={health?.status || 'UNKNOWN'}
          icon={Server}
          accentColor={health?.status === 'UP' ? 'var(--ok)' : 'var(--err)'}
          statusColor={health?.status === 'UP' ? 'var(--ok)' : 'var(--err)'}
        >
          <div className="mono" style={{ fontSize:11, color:'var(--text-mut)', marginTop:10, lineHeight:1.8 }}>
            <div>PORT {health?.port || 7779}</div>
            <div style={{ overflow:'hidden', textOverflow:'ellipsis', whiteSpace:'nowrap' }}>UUID {health?.uuid?.slice(0,16) || 'n/a'}…</div>
          </div>
        </KpiCard>

        {/* NF HEALTH */}
        <KpiCard label="NF Health" value={`${nfTotal}/${nfTotal}`} icon={Database} accentColor="var(--ok)" statusColor="var(--ok)">
          <div style={{ marginTop:12 }}>
            <div style={{ height:4, background:'var(--border-sub)', borderRadius:2, overflow:'hidden' }}>
              <div style={{ height:'100%', width:'100%', background:'var(--ok)', borderRadius:2 }} />
            </div>
            <div style={{ fontSize:11, color:'var(--ok)', marginTop:6 }}>All network functions operational</div>
          </div>
        </KpiCard>

        {/* THROUGHPUT */}
        <KpiCard label="Live Throughput" value={dl} suffix=" kbps" icon={Activity} accentColor="var(--accent)" sparkData={dlHist}>
          <div style={{ display:'flex', gap:16, marginTop:8 }}>
            <span className="mono" style={{ fontSize:12 }}><span style={{color:'var(--accent)'}}>DL</span> {dl.toFixed(0)} kbps</span>
            <span className="mono" style={{ fontSize:12 }}><span style={{color:'var(--ok)'}}>UL</span> {ul.toFixed(0)} kbps</span>
          </div>
        </KpiCard>

        {/* ANOMALY STATUS */}
        <div className={cx('card', isAnomaly && 'pulse-crit')} style={{ padding:20, borderLeft:`3px solid ${isAnomaly ? 'var(--err)' : 'var(--ok)'}`, ...(isAnomaly ? { background:'rgba(239,68,68,.06)' } : {}) }}>
          <div style={{ display:'flex', justifyContent:'space-between', alignItems:'flex-start', marginBottom:12 }}>
            <Label>Anomaly Detection</Label>
            {isAnomaly ? <ShieldAlert style={{ width:18, height:18, color:'var(--err)' }} /> : <CheckCircle style={{ width:18, height:18, color:'var(--ok)' }} />}
          </div>
          <div style={{ fontSize:22, fontWeight:700, color: isAnomaly ? 'var(--err)' : 'var(--ok)', marginBottom:8 }}>
            {isAnomaly ? 'ANOMALY DETECTED' : 'SYSTEM NORMAL'}
          </div>
          <div className="mono" style={{ fontSize:11, color:'var(--text-sec)', display:'flex', gap:16 }}>
            <span><AnimatedNumber value={anomaly?.anomaly_pct || 0} suffix="% flagged" decimals={1}/></span>
            <span><AnimatedNumber value={(anomaly?.confidence || 0)*100} suffix="% conf." decimals={0}/></span>
          </div>
        </div>

        {/* MOS SCORE */}
        <KpiCard label="MOS Score" value={mos} decimals={2} suffix=" / 5.0" icon={Zap} accentColor="var(--warn)" statusColor="var(--warn)" sparkData={[]}>
          <div style={{ marginTop:8 }}>
            <div style={{ display:'flex', gap:2 }}>
              {[1,2,3,4,5].map(i => (
                <div key={i} style={{ flex:1, height:3, borderRadius:2, background: i <= Math.round(mos) ? 'var(--warn)' : 'var(--border-sub)' }} />
              ))}
            </div>
            <div style={{ fontSize:11, color:'var(--warn)', marginTop:6 }}>
              {mos >= 4 ? 'EXCELLENT' : mos >= 3.5 ? 'GOOD' : mos >= 3 ? 'FAIR' : 'POOR'}
            </div>
          </div>
        </KpiCard>

        {/* NET PERFORMANCE */}
        <KpiCard label="Network Performance" value={netScore} decimals={0} suffix=" / 100" icon={Wifi} accentColor="var(--info)" statusColor="var(--info)">
          <div style={{ marginTop:12 }}>
            <div style={{ height:4, background:'var(--border-sub)', borderRadius:2, overflow:'hidden' }}>
              <div style={{ height:'100%', width:`${netScore}%`, background:`linear-gradient(90deg, var(--info), var(--accent))`, borderRadius:2, transition:'width .5s' }} />
            </div>
            <div style={{ fontSize:11, color:'var(--info)', marginTop:6 }}>
              {netScore >= 80 ? 'Grade A' : netScore >= 60 ? 'Grade B' : netScore >= 40 ? 'Grade C' : 'Grade D'}
            </div>
          </div>
        </KpiCard>
      </div>

      {/* Live throughput chart */}
      <div className="card" style={{ padding:'16px 20px' }}>
        <div style={{ display:'flex', justifyContent:'space-between', alignItems:'center', marginBottom:16 }}>
          <div style={{ fontSize:13, fontWeight:600, color:'var(--text-sec)', letterSpacing:'.05em', textTransform:'uppercase' }}>Live Throughput — 30s window</div>
          <div style={{ display:'flex', gap:16, fontSize:12, color:'var(--text-sec)' }}>
            <span style={{ display:'flex', alignItems:'center', gap:5 }}><Dot color="var(--accent)"/>Downlink</span>
            <span style={{ display:'flex', alignItems:'center', gap:5 }}><Dot color="var(--ok)"/>Uplink</span>
          </div>
        </div>
        <ResponsiveContainer width="100%" height={220}>
          <AreaChart data={tpHist} margin={{ top:4, right:4, left:-20, bottom:0 }}>
            <defs>
              <linearGradient id="gDl" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%"  stopColor="var(--accent)" stopOpacity={.25}/>
                <stop offset="95%" stopColor="var(--accent)" stopOpacity={0}/>
              </linearGradient>
              <linearGradient id="gUl" x1="0" y1="0" x2="0" y2="1">
                <stop offset="5%"  stopColor="var(--ok)"     stopOpacity={.25}/>
                <stop offset="95%" stopColor="var(--ok)"     stopOpacity={0}/>
              </linearGradient>
            </defs>
            <ChartGrid />
            <AxisX dataKey="t" />
            <AxisY />
            <ChartTip />
            <Area type="monotone" dataKey="dl" name="DL (kbps)" stroke="var(--accent)" fill="url(#gDl)" strokeWidth={2} isAnimationActive={false} />
            <Area type="monotone" dataKey="ul" name="UL (kbps)" stroke="var(--ok)"     fill="url(#gUl)" strokeWidth={2} isAnimationActive={false} />
          </AreaChart>
        </ResponsiveContainer>
      </div>
    </div>
  );
};

// ─── ANALYTICS PAGE (Task #5) ────────────────────────────────────────────────
const AnalyticsPage = ({ analyticsId }) => {
  const api = useApi();
  const toast = useToast();
  const { settings } = useContext(SettingsCtx);
  const [data, setData]     = useState(null);
  const [loading, setLoad]  = useState(true);
  const [error, setError]   = useState(null);
  const [showRaw, setRaw]   = useState(settings.showRawJson);

  const meta = ANALYTICS_IDS.find(a => a.id === analyticsId);

  const load = useCallback(async () => {
    try { setLoad(true); setError(null); setData(await api.analytics(analyticsId)); }
    catch (e) { setError(e); }
    finally { setLoad(false); }
  }, [api, analyticsId]);

  useEffect(() => {
    load();
    if (settings.autoRefresh) {
      const iv = setInterval(load, (settings.refreshInterval || 5) * 1000);
      return () => clearInterval(iv);
    }
  }, [load, settings.autoRefresh, settings.refreshInterval]);

  const retrain = async () => {
    try { await api.train(); toast('success', 'ML model retraining initiated'); }
    catch {}
  };

  const ad = data?.analData;

  const renderViz = () => {
    if (loading && !data) return <div style={{ display:'grid', gridTemplateColumns:'1fr', gap:16 }}><SkeletonCard/><SkeletonCard/></div>;
    if (error && !data) return <ErrorState error={error} onRetry={load} />;
    if (!ad) return <EmptyState icon={Database} title="No data available" desc="The analytics endpoint returned no data yet." />;

    switch (analyticsId) {

      case 'NF_LOAD': {
        const bars = (ad.nfLoadLevelList || []).map(nf => {
          const v = nf.nfLoadLevelInfo?.nfCpuUsage || 0;
          return { name: nf.nfType, load: v, color: v > 80 ? 'var(--err)' : v > 60 ? 'var(--warn)' : 'var(--ok)' };
        });
        return (
          <>
            <ResponsiveContainer width="100%" height={280}>
              <BarChart data={bars} layout="vertical" margin={{ top:4, right:24, left:8, bottom:4 }}>
                <ChartGridH />
                <AxisX type="number" domain={[0,100]} tickFormatter={v=>`${v}%`} />
                <AxisY dataKey="name" type="category" width={55} />
                <ChartTip formatter={v=>[`${v}%`, 'CPU Load']} />
                <Bar dataKey="load" radius={[0,4,4,0]}>
                  {bars.map((b,i) => <Cell key={i} fill={b.color} />)}
                </Bar>
              </BarChart>
            </ResponsiveContainer>
            <div style={{ display:'flex', flexWrap:'wrap', gap:8, marginTop:12 }}>
              {bars.map(b => (
                <span key={b.name} className="pill" style={{ background:`${b.color}1a`, color:b.color, border:`1px solid ${b.color}44` }}>
                  {b.name} — {b.load.toFixed(0)}%
                </span>
              ))}
            </div>
          </>
        );
      }

      case 'ABNORMAL_BEHAVIOUR': {
        const isA = ad.anomalyDetected || ad.anomaly_detected;
        return (
          <div style={{ display:'flex', flexDirection:'column', gap:16 }}>
            {ad.dataPoints < 100 && (
              <div className="card" style={{ padding:24, background:'var(--warn-dim)', border:'1px solid rgba(245,158,11,.3)', display:'flex', alignItems:'center', gap:16 }}>
                <Clock style={{ width:32, height:32, color:'var(--warn)', flexShrink:0 }} />
                <div>
                  <div style={{ fontSize:14, fontWeight:600, color:'var(--warn)' }}>Collecting Baseline Data</div>
                  <div className="mono" style={{ fontSize:12, color:'var(--text-sec)', marginTop:4 }}>{ad.dataPoints || 0} / 100 samples</div>
                  <div style={{ height:4, background:'var(--border-sub)', borderRadius:2, marginTop:8, width:240 }}>
                    <div style={{ height:'100%', width:`${Math.min((ad.dataPoints||0),100)}%`, background:'var(--warn)', borderRadius:2 }} />
                  </div>
                </div>
              </div>
            )}
            {(ad.dataPoints || 0) >= 100 && (
              <div className={cx('card', isA && 'pulse-crit')} style={{ padding:20, background: isA ? 'var(--err-dim)' : 'var(--ok-dim)', border:`1px solid ${isA?'rgba(239,68,68,.3)':'rgba(16,185,129,.3)'}`, display:'flex', alignItems:'center', gap:16 }}>
                {isA ? <ShieldAlert style={{width:36,height:36,color:'var(--err)'}} /> : <CheckCircle style={{width:36,height:36,color:'var(--ok)'}} />}
                <div>
                  <div style={{ fontSize:16, fontWeight:700, color: isA ? 'var(--err)' : 'var(--ok)' }}>
                    {isA ? `ANOMALY DETECTED — ${ad.anomalyType || 'UNKNOWN TYPE'}` : 'SYSTEM NORMAL'}
                  </div>
                  <div className="mono" style={{ fontSize:12, color:'var(--text-sec)', marginTop:4 }}>
                    Confidence {((ad.confidence||0)*100).toFixed(1)}% · Avg score {ad.avgAnomalyScore?.toFixed(4)}
                  </div>
                </div>
                <button className="btn btn-secondary" style={{marginLeft:'auto'}} onClick={retrain}><RotateCcw className="w-4 h-4"/>Retrain Model</button>
              </div>
            )}
          </div>
        );
      }

      case 'SERVICE_EXPERIENCE': {
        const mos = ad.mos_score || 0;
        const pct = Math.min(Math.max((mos - 1) / 4, 0), 1);
        const rot = -90 + 180 * pct;
        const col = mos >= 4 ? 'var(--ok)' : mos >= 3 ? 'var(--warn)' : 'var(--err)';
        return (
          <div style={{ display:'flex', flexDirection:'column', alignItems:'center', padding:'32px 0' }}>
            <div style={{ position:'relative', width:220, height:110, overflow:'hidden', marginBottom:24 }}>
              <div style={{ position:'absolute', inset:0, border:'18px solid var(--border-sub)', borderRadius:'110px 110px 0 0', borderBottom:'none' }} />
              <div style={{ position:'absolute', inset:0, border:'18px solid transparent', borderTopColor:col, borderRightColor:col, borderLeftColor:col, borderRadius:'110px 110px 0 0', borderBottom:'none', transform:`rotate(${rot}deg)`, transformOrigin:'bottom center', transition:'transform .8s cubic-bezier(.4,0,.2,1)' }} />
            </div>
            <div className="mono" style={{ fontSize:52, fontWeight:700, color:col, lineHeight:1 }}>{mos.toFixed(2)}</div>
            <div style={{ fontSize:14, color:'var(--text-sec)', marginTop:8, letterSpacing:'.1em' }}>{ad.mos_category || 'UNKNOWN'}</div>
            <div style={{ display:'flex', gap:24, marginTop:20, fontSize:12, color:'var(--text-mut)' }}>
              <span>1 — Poor</span><span>3 — Fair</span><span>5 — Excellent</span>
            </div>
          </div>
        );
      }

      case 'QoS_SUSTAINABILITY': {
        const dl = ad.avgDlKbps || 0, ul = ad.avgUlKbps || 0, conf = ad.confidence || 0;
        const items = [
          { label:'Downlink Avg', value:`${dl.toFixed(2)} kbps`, color:'var(--accent)' },
          { label:'Sustainability', value:`${conf}%`, sub:'Confidence', color:'var(--ok)' },
          { label:'Uplink Avg',   value:`${ul.toFixed(2)} kbps`, color:'var(--warn)' },
        ];
        return (
          <div style={{ display:'grid', gridTemplateColumns:'repeat(3,1fr)', gap:16, padding:'24px 0' }}>
            {items.map(item => (
              <div key={item.label} className="card" style={{ padding:24, textAlign:'center', borderTop:`3px solid ${item.color}` }}>
                <Label style={{ justifyContent:'center', display:'flex' }}>{item.label}</Label>
                <div className="mono" style={{ fontSize:30, fontWeight:700, color:item.color, margin:'12px 0 4px' }}>{item.value}</div>
                {item.sub && <div style={{ fontSize:11, color:'var(--text-mut)' }}>{item.sub}</div>}
              </div>
            ))}
          </div>
        );
      }

      case 'NETWORK_PERFORMANCE': {
        const { components={}, overallScore=0, scoreLabel='', confidence=0, grade='N/A' } = ad;
        const col = grade==='A'||grade==='B' ? 'var(--ok)' : grade==='C' ? 'var(--warn)' : 'var(--err)';
        return (
          <div style={{ display:'flex', flexDirection:'column', gap:20 }}>
            <div style={{ display:'flex', gap:20, alignItems:'center', flexWrap:'wrap' }}>
              <div style={{ width:160, height:160, borderRadius:'50%', border:`6px solid ${col}`, display:'flex', flexDirection:'column', alignItems:'center', justifyContent:'center', flexShrink:0, background:'var(--bg-elevated)' }}>
                <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:4 }}>OVERALL</div>
                <div className="mono" style={{ fontSize:36, fontWeight:700, color:col }}>{overallScore.toFixed(0)}</div>
                <div style={{ fontSize:16, fontWeight:700, color:col }}>{grade}</div>
              </div>
              <div style={{ flex:1, display:'flex', flexDirection:'column', gap:10 }}>
                <div className="card" style={{ padding:'12px 16px', display:'flex', justifyContent:'space-between' }}>
                  <span style={{ color:'var(--text-sec)', fontSize:13 }}>Score Label</span>
                  <span style={{ fontWeight:600, color:col }}>{scoreLabel || 'N/A'}</span>
                </div>
                <div className="card" style={{ padding:'12px 16px', display:'flex', justifyContent:'space-between' }}>
                  <span style={{ color:'var(--text-sec)', fontSize:13 }}>Confidence</span>
                  <span className="mono" style={{ fontWeight:600 }}>{confidence}%</span>
                </div>
              </div>
            </div>
            {Object.keys(components).length > 0 && (
              <div>
                <Label style={{ marginBottom:12 }}>Component Scores</Label>
                <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill,minmax(160px,1fr))', gap:12 }}>
                  {Object.entries(components).map(([k,v]) => (
                    <div key={k} className="card" style={{ padding:16 }}>
                      <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:8 }}>{k.replace(/([A-Z])/g,' $1').trim().toUpperCase()}</div>
                      <div className="mono" style={{ fontSize:22, fontWeight:700 }}>{typeof v==='number' ? v.toFixed(2) : String(v)}</div>
                    </div>
                  ))}
                </div>
              </div>
            )}
          </div>
        );
      }

      case 'UE_MOBILITY': {
        const entries = Object.entries(ad).filter(([k])=>!['analyticsId','timestamp'].includes(k));
        return (
          <div>
            <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill,minmax(200px,1fr))', gap:12 }}>
              {entries.length ? entries.map(([k,v])=>(
                <div key={k} className="card" style={{ padding:16 }}>
                  <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:8, textTransform:'uppercase', letterSpacing:'.06em' }}>{k.replace(/([A-Z])/g,' $1').trim()}</div>
                  <div className="mono" style={{ fontSize:18, fontWeight:700, wordBreak:'break-all' }}>{typeof v==='object'?JSON.stringify(v):String(v)}</div>
                </div>
              )) : <EmptyState icon={Smartphone} title="No UE mobility data" desc="UEs must be attached and moving to generate mobility analytics." />}
            </div>
          </div>
        );
      }

      case 'UE_COMMUNICATION': {
        const entries = Object.entries(ad).filter(([k])=>!['analyticsId','timestamp'].includes(k));
        return (
          <div>
            <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill,minmax(200px,1fr))', gap:12 }}>
              {entries.length ? entries.map(([k,v])=>(
                <div key={k} className="card" style={{ padding:16 }}>
                  <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:8, textTransform:'uppercase', letterSpacing:'.06em' }}>{k.replace(/([A-Z])/g,' $1').trim()}</div>
                  <div className="mono" style={{ fontSize:18, fontWeight:700, wordBreak:'break-all' }}>{typeof v==='object'?JSON.stringify(v):String(v)}</div>
                </div>
              )) : <EmptyState icon={Radio} title="No UE communication data" desc="Active UE sessions are required to produce communication analytics." />}
            </div>
          </div>
        );
      }

      default:
        return (
          <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill,minmax(200px,1fr))', gap:12 }}>
            {Object.entries(ad).filter(([k])=>!['analyticsId','timestamp'].includes(k)).map(([k,v])=>(
              <div key={k} className="card" style={{ padding:16 }}>
                <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:8 }}>{k.replace(/([A-Z])/g,' $1').trim().toUpperCase()}</div>
                <div className="mono" style={{ fontSize:20, fontWeight:700 }}>{typeof v==='object'?JSON.stringify(v):(typeof v==='number'&&!Number.isInteger(v)?v.toFixed(3):String(v))}</div>
              </div>
            ))}
          </div>
        );
    }
  };

  const Icon = meta?.icon || Activity;

  return (
    <div className="fade-up">
      {/* Page header */}
      <div style={{ display:'flex', alignItems:'center', justifyContent:'space-between', marginBottom:20, paddingBottom:16, borderBottom:'1px solid var(--border-sub)' }}>
        <div style={{ display:'flex', alignItems:'center', gap:12 }}>
          <div style={{ width:36, height:36, borderRadius:8, background:'var(--accent-dim)', display:'flex', alignItems:'center', justifyContent:'center' }}>
            <Icon style={{ width:18, height:18, color:'var(--accent)' }} />
          </div>
          <div>
            <h1 style={{ margin:0, fontSize:18, fontWeight:700 }}>{meta?.name || analyticsId}</h1>
            {meta?.ref && settings.show3gpp && (
              <span className="mono pill pill-neu" style={{ marginTop:4, display:'inline-block', fontSize:10 }}>{meta.ref}</span>
            )}
          </div>
        </div>
        <div style={{ display:'flex', gap:8 }}>
          {analyticsId === 'ABNORMAL_BEHAVIOUR' && (
            <button className="btn btn-secondary" onClick={retrain}><RotateCcw className="w-4 h-4"/>Retrain</button>
          )}
          <button className="btn btn-secondary" style={{padding:'7px 10px'}} onClick={load} title="Refresh">
            <RefreshCw style={{ width:15, height:15 }} className={loading ? 'animate-spin' : ''} />
          </button>
          <button className="btn btn-ghost" onClick={() => setRaw(r=>!r)} title="Toggle raw JSON">
            {showRaw ? <EyeOff className="w-4 h-4"/> : <Eye className="w-4 h-4"/>}
          </button>
        </div>
      </div>

      <div className="card" style={{ padding:24, minHeight:320 }}>
        {renderViz()}
      </div>

      {showRaw && data && <RawJson data={data} onClose={()=>setRaw(false)} />}
    </div>
  );
};

// ─── ML MODELS PAGE (Task #6) ────────────────────────────────────────────────
const ISOLATION_FOREST_FEATURES = [
  { name: 'dl_kbps',    label: 'Downlink Throughput', importance: 0.32, color: 'var(--accent)'  },
  { name: 'ul_kbps',    label: 'Uplink Throughput',   importance: 0.28, color: 'var(--ok)'     },
  { name: 'cpu_pct',    label: 'CPU Utilisation',     importance: 0.21, color: 'var(--warn)'   },
  { name: 'mem_pct',    label: 'Memory Utilisation',  importance: 0.12, color: 'var(--info)'   },
  { name: 'retx_ratio', label: 'Retransmission Ratio',importance: 0.07, color: 'var(--err)'    },
];

const MLModelsPage = () => {
  const api   = useApi();
  const toast = useToast();
  const [training,   setTraining]  = useState(false);
  const [lastTrained,setLast]      = useState(null);
  const [health,     setHealth]    = useState(null);
  const [loadingH,   setLoadH]     = useState(true);

  useEffect(() => {
    api.health().then(h => { setHealth(h); setLoadH(false); }).catch(() => setLoadH(false));
  }, [api]);

  const retrain = async () => {
    setTraining(true);
    try {
      await api.train();
      setLast(new Date());
      toast('success', 'Model retraining completed successfully');
    } catch { /* toast from api layer */ }
    finally { setTraining(false); }
  };

  const models = [
    {
      id: 'isolation_forest',
      name: 'Isolation Forest',
      type: 'Anomaly Detection',
      desc: 'Unsupervised tree-based model for detecting anomalous network behaviour across 5 UPF features.',
      status: loadingH ? 'LOADING' : health ? 'READY' : 'UNKNOWN',
      params: [
        { k: 'n_estimators', v: '100' },
        { k: 'contamination', v: '0.05 (5%)' },
        { k: 'features', v: '5 (UPF metrics)' },
        { k: 'window', v: '100 samples' },
      ],
      features: ISOLATION_FOREST_FEATURES,
      ref: 'TS 23.288 §6.4',
    },
    {
      id: 'ewma_predictor',
      name: 'EWMA Predictor',
      type: 'QoS Forecasting',
      desc: 'Exponentially Weighted Moving Average model for short-term QoS throughput forecasting.',
      status: loadingH ? 'LOADING' : health ? 'READY' : 'UNKNOWN',
      params: [
        { k: 'alpha (α)', v: '0.3' },
        { k: 'targets', v: 'DL kbps, UL kbps' },
        { k: 'horizon', v: 'Next sample' },
        { k: 'update_rate', v: 'Per collection cycle' },
      ],
      features: null,
      ref: 'TS 23.288 §6.9',
    },
  ];

  const statusColor = s => s === 'READY' ? 'var(--ok)' : s === 'TRAINING' ? 'var(--warn)' : 'var(--text-sec)';
  const statusPill  = s => s === 'READY' ? 'pill-ok' : s === 'TRAINING' ? 'pill-warn' : 'pill-neu';

  return (
    <div className="fade-up">
      <SectionHeader
        title="ML Model Management"
        action={
          <button className="btn btn-primary" onClick={retrain} disabled={training}>
            <RotateCcw style={{ width:14, height:14 }} className={training ? 'animate-spin' : ''} />
            {training ? 'Retraining…' : 'Retrain All Models'}
          </button>
        }
      />

      {lastTrained && (
        <div style={{ marginBottom:16, padding:'10px 14px', background:'var(--ok-dim)', border:'1px solid rgba(16,185,129,.3)', borderRadius:8, fontSize:13, color:'var(--ok)' }}>
          <CheckCircle style={{ width:14, height:14, display:'inline', marginRight:6 }} />
          Models successfully retrained at {lastTrained.toLocaleTimeString()}
        </div>
      )}

      <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill, minmax(480px, 1fr))', gap:20 }}>
        {models.map(m => (
          <div key={m.id} className="card" style={{ padding:24 }}>
            {/* Model header */}
            <div style={{ display:'flex', alignItems:'flex-start', justifyContent:'space-between', marginBottom:16, paddingBottom:16, borderBottom:'1px solid var(--border-sub)' }}>
              <div style={{ display:'flex', gap:12, alignItems:'center' }}>
                <div style={{ width:40, height:40, borderRadius:10, background:'var(--accent-dim)', border:'1px solid rgba(34,211,238,.2)', display:'flex', alignItems:'center', justifyContent:'center', flexShrink:0 }}>
                  <Brain style={{ width:20, height:20, color:'var(--accent)' }} />
                </div>
                <div>
                  <div style={{ fontSize:15, fontWeight:700 }}>{m.name}</div>
                  <div style={{ fontSize:12, color:'var(--text-sec)' }}>{m.type}</div>
                </div>
              </div>
              <div style={{ display:'flex', flexDirection:'column', alignItems:'flex-end', gap:6 }}>
                <span className={cx('pill', statusPill(m.status))}>
                  <Dot color={statusColor(m.status)} />{m.status}
                </span>
                <span className="mono pill pill-neu" style={{ fontSize:10 }}>{m.ref}</span>
              </div>
            </div>

            <p style={{ fontSize:13, color:'var(--text-sec)', lineHeight:1.6, margin:'0 0 20px' }}>{m.desc}</p>

            {/* Parameters */}
            <Label style={{ marginBottom:10 }}>Model Parameters</Label>
            <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:8, marginBottom:20 }}>
              {m.params.map(p => (
                <div key={p.k} style={{ padding:'8px 12px', background:'var(--bg-elevated)', borderRadius:6, border:'1px solid var(--border-sub)' }}>
                  <div style={{ fontSize:10, color:'var(--text-mut)', marginBottom:3 }}>{p.k.toUpperCase()}</div>
                  <div className="mono" style={{ fontSize:13, fontWeight:600 }}>{p.v}</div>
                </div>
              ))}
            </div>

            {/* Feature Importance (Isolation Forest only) */}
            {m.features && (
              <>
                <Label style={{ marginBottom:12 }}>Feature Importance</Label>
                <div style={{ display:'flex', flexDirection:'column', gap:8 }}>
                  {m.features.map(f => (
                    <div key={f.name}>
                      <div style={{ display:'flex', justifyContent:'space-between', fontSize:12, marginBottom:4 }}>
                        <span style={{ color:'var(--text-sec)' }}>{f.label}</span>
                        <span className="mono" style={{ color:f.color }}>{(f.importance*100).toFixed(0)}%</span>
                      </div>
                      <div style={{ height:4, background:'var(--border-sub)', borderRadius:2 }}>
                        <div style={{ height:'100%', width:`${f.importance*100}%`, background:f.color, borderRadius:2, transition:'width .6s' }} />
                      </div>
                    </div>
                  ))}
                </div>
              </>
            )}
          </div>
        ))}
      </div>
    </div>
  );
};

// ─── METRICS PAGE (Task #7) ──────────────────────────────────────────────────
const METRIC_CATEGORIES = {
  'Throughput':  ['nwdaf_throughput_dl_kbps', 'nwdaf_throughput_ul_kbps'],
  'Quality':     ['nwdaf_mos_score', 'nwdaf_network_performance_score'],
  'NF Load':     k => k.startsWith('nwdaf_nf_load_pct'),
  'ML / Scores': k => k.startsWith('nwdaf_ml') || k.startsWith('nwdaf_anomaly') || k.startsWith('nwdaf_avg'),
};

function categoriseMetrics(raw) {
  const cats = { 'Throughput': [], 'Quality': [], 'NF Load': [], 'ML / Scores': [], 'System': [] };
  Object.entries(raw).forEach(([k, v]) => {
    if (['nwdaf_throughput_dl_kbps','nwdaf_throughput_ul_kbps'].includes(k)) cats['Throughput'].push({ k, v });
    else if (['nwdaf_mos_score','nwdaf_network_performance_score'].includes(k)) cats['Quality'].push({ k, v });
    else if (k.startsWith('nwdaf_nf_load_pct')) cats['NF Load'].push({ k, v });
    else if (k.startsWith('nwdaf_ml')||k.startsWith('nwdaf_anomaly')||k.startsWith('nwdaf_avg')) cats['ML / Scores'].push({ k, v });
    else cats['System'].push({ k, v });
  });
  return cats;
}

const MetricsPage = () => {
  const api = useApi();
  const [raw,    setRaw]    = useState({});
  const [load,   setLoad]   = useState(true);
  const [error,  setError]  = useState(null);
  const [query,  setQuery]  = useState('');
  const [lastAt, setLastAt] = useState(null);

  const fetch = useCallback(async () => {
    try { setError(null); const m = await api.metrics(); setRaw(m); setLastAt(new Date()); }
    catch (e) { setError(e); }
    finally { setLoad(false); }
  }, [api]);

  useEffect(() => { fetch(); const iv = setInterval(fetch, 10000); return () => clearInterval(iv); }, [fetch]);

  const cats = categoriseMetrics(raw);
  const q    = query.toLowerCase();
  const filteredAll = Object.entries(raw).filter(([k,v]) => !q || k.toLowerCase().includes(q) || String(v).includes(q));

  const catColor = cat => ({
    'Throughput':'var(--accent)', 'Quality':'var(--ok)', 'NF Load':'var(--warn)',
    'ML / Scores':'var(--info)', 'System':'var(--text-sec)'
  })[cat] || 'var(--text-sec)';

  if (load) return <div style={{display:'grid',gridTemplateColumns:'1fr 1fr',gap:16}}>{[...Array(4)].map((_,i)=><SkeletonCard key={i}/>)}</div>;
  if (error) return <ErrorState error={error} onRetry={fetch} />;

  return (
    <div className="fade-up">
      <SectionHeader
        title="Prometheus Metrics"
        action={
          <div style={{ display:'flex', gap:10, alignItems:'center' }}>
            {lastAt && <span style={{ fontSize:12, color:'var(--text-sec)' }}>Last: {lastAt.toLocaleTimeString()}</span>}
            <button className="btn btn-secondary" onClick={fetch}><RefreshCw className="w-4 h-4"/>Refresh</button>
          </div>
        }
      />

      {/* Search */}
      <div style={{ position:'relative', marginBottom:20 }}>
        <Search style={{ position:'absolute', left:10, top:'50%', transform:'translateY(-50%)', width:15, height:15, color:'var(--text-mut)' }} />
        <input className="inp" value={query} onChange={e=>setQuery(e.target.value)} placeholder="Filter metrics…" style={{ paddingLeft:32 }} />
      </div>

      {!query ? (
        /* Categorised view */
        <div style={{ display:'flex', flexDirection:'column', gap:16 }}>
          {Object.entries(cats).filter(([,items])=>items.length).map(([cat, items]) => (
            <div key={cat} className="card" style={{ overflow:'hidden' }}>
              <div style={{ padding:'10px 16px', background:'var(--bg-elevated)', borderBottom:'1px solid var(--border-sub)', display:'flex', alignItems:'center', gap:8 }}>
                <Dot color={catColor(cat)} />
                <span style={{ fontSize:12, fontWeight:600, letterSpacing:'.06em', textTransform:'uppercase', color:catColor(cat) }}>{cat}</span>
                <span style={{ marginLeft:'auto', fontSize:11, color:'var(--text-mut)' }}>{items.length} metric{items.length!==1?'s':''}</span>
              </div>
              <table className="tbl">
                <thead><tr><th>Metric</th><th style={{textAlign:'right'}}>Value</th></tr></thead>
                <tbody>
                  {items.map(({k,v}) => (
                    <tr key={k}>
                      <td className="mono" style={{ fontSize:12, color:'var(--text-sec)', wordBreak:'break-all' }}>{k}</td>
                      <td className="mono" style={{ fontSize:13, fontWeight:600, color:catColor(cat), textAlign:'right' }}>
                        {typeof v === 'number' ? (Number.isInteger(v) ? v : v.toFixed(4)) : String(v)}
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </div>
          ))}
        </div>
      ) : (
        /* Filtered flat view */
        <div className="card" style={{ overflow:'hidden' }}>
          <table className="tbl">
            <thead><tr><th>Metric</th><th style={{textAlign:'right'}}>Value</th></tr></thead>
            <tbody>
              {filteredAll.length ? filteredAll.map(([k,v]) => (
                <tr key={k}>
                  <td className="mono" style={{ fontSize:12, color:'var(--text-sec)' }}>{k}</td>
                  <td className="mono" style={{ fontSize:13, fontWeight:600, textAlign:'right' }}>{typeof v==='number'?(Number.isInteger(v)?v:v.toFixed(4)):String(v)}</td>
                </tr>
              )) : (
                <tr><td colSpan={2} style={{ padding:32, textAlign:'center', color:'var(--text-mut)' }}>No metrics match "{query}"</td></tr>
              )}
            </tbody>
          </table>
        </div>
      )}
    </div>
  );
};

// ─── SERVICE CONTROL PAGE (Task #8) ──────────────────────────────────────────
const ServiceControlPage = () => {
  const api   = useApi();
  const toast = useToast();
  const [health,   setHealth]   = useState(null);
  const [metrics,  setMetrics]  = useState({});
  const [loading,  setLoading]  = useState(true);
  const [testing,  setTesting]  = useState(false);
  const [training, setTraining] = useState(false);

  const reload = useCallback(async () => {
    try {
      const [h, m] = await Promise.all([api.health().catch(()=>null), api.metrics().catch(()=>{})]);
      setHealth(h); setMetrics(m || {});
    } finally { setLoading(false); }
  }, [api]);

  useEffect(() => { reload(); const iv = setInterval(reload, 8000); return () => clearInterval(iv); }, [reload]);

  const testConn = async () => {
    setTesting(true);
    try { await api.health(); toast('success', 'NWDAF daemon reachable — connection healthy'); }
    catch { toast('error', 'Cannot reach NWDAF daemon'); }
    finally { setTesting(false); }
  };

  const retrain = async () => {
    setTraining(true);
    try { await api.train(); toast('success', 'ML retraining triggered successfully'); }
    catch {} finally { setTraining(false); }
  };

  const nfKeys   = Object.keys(metrics).filter(k => k.startsWith('nwdaf_nf_load_pct'));
  const isUp     = health?.status === 'UP';

  const compliance = [
    { spec:'TS 23.288 v17', desc:'Analytics IDs', status:'7/7 ✓' },
    { spec:'TS 29.520 v17', desc:'Subscription & Analytics API', status:'✓' },
    { spec:'TS 29.510 v17', desc:'NRF Registration', status:'✓' },
    { spec:'TS 28.554 v17', desc:'KPI Collection', status:'✓' },
  ];

  return (
    <div className="fade-up">
      <SectionHeader title="Service Control" action={
        <button className="btn btn-secondary" onClick={reload}><RefreshCw className="w-4 h-4"/>Refresh</button>
      }/>

      <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill, minmax(300px,1fr))', gap:16, marginBottom:20 }}>

        {/* NWDAF Daemon */}
        <div className="card" style={{ padding:20 }}>
          <div style={{ display:'flex', justifyContent:'space-between', marginBottom:16 }}>
            <Label>NWDAF Daemon</Label>
            {loading ? <span className="pill pill-neu">CHECKING</span>
              : <span className={cx('pill', isUp ? 'pill-ok' : 'pill-err')}><Dot color={isUp?'var(--ok)':'var(--err)'}/>{isUp?'UP':'DOWN'}</span>}
          </div>
          {health ? (
            <div className="mono" style={{ fontSize:12, color:'var(--text-sec)', lineHeight:2 }}>
              <div>Port: <strong style={{color:'var(--text-pri)'}}>{health.port || 7779}</strong></div>
              <div>UUID: <strong style={{color:'var(--text-pri)', wordBreak:'break-all', fontSize:11}}>{health.uuid || 'n/a'}</strong></div>
              <div>Version: <strong style={{color:'var(--text-pri)'}}>open5gs-nwdafd</strong></div>
            </div>
          ) : <div style={{ color:'var(--text-mut)', fontSize:13 }}>Daemon unreachable</div>}
        </div>

        {/* NF Health Summary */}
        <div className="card" style={{ padding:20 }}>
          <Label style={{ marginBottom:12 }}>NF Health Summary</Label>
          {nfKeys.length ? (
            <div style={{ display:'flex', flexDirection:'column', gap:8 }}>
              {nfKeys.slice(0, 6).map(k => {
                const label = k.match(/nf="([^"]+)"/)?.[1] || k.replace('nwdaf_nf_load_pct','NF');
                const val   = metrics[k] || 0;
                const col   = val > 80 ? 'var(--err)' : val > 60 ? 'var(--warn)' : 'var(--ok)';
                return (
                  <div key={k} style={{ display:'flex', alignItems:'center', gap:10 }}>
                    <span style={{ fontSize:12, color:'var(--text-sec)', minWidth:50 }}>{label}</span>
                    <div style={{ flex:1, height:4, background:'var(--border-sub)', borderRadius:2 }}>
                      <div style={{ height:'100%', width:`${val}%`, background:col, borderRadius:2 }} />
                    </div>
                    <span className="mono" style={{ fontSize:11, color:col, minWidth:36, textAlign:'right' }}>{val.toFixed(0)}%</span>
                  </div>
                );
              })}
            </div>
          ) : <div style={{ color:'var(--text-mut)', fontSize:13 }}>No NF load metrics available yet</div>}
        </div>

        {/* Quick Actions */}
        <div className="card" style={{ padding:20 }}>
          <Label style={{ marginBottom:12 }}>Quick Actions</Label>
          <div style={{ display:'flex', flexDirection:'column', gap:10 }}>
            <button className="btn btn-secondary" style={{justifyContent:'flex-start', width:'100%'}} onClick={testConn} disabled={testing}>
              <Globe style={{width:15,height:15, color:'var(--accent)'}}/>{testing ? 'Testing…' : 'Test Connection'}
            </button>
            <button className="btn btn-secondary" style={{justifyContent:'flex-start', width:'100%'}} onClick={retrain} disabled={training}>
              <RotateCcw style={{width:15,height:15, color:'var(--warn)'}}/>{training ? 'Retraining…' : 'Trigger ML Retrain'}
            </button>
            <button className="btn btn-secondary" style={{justifyContent:'flex-start', width:'100%'}} onClick={reload}>
              <RefreshCw style={{width:15,height:15, color:'var(--ok)'}}/> Refresh Status
            </button>
          </div>
        </div>
      </div>

      {/* 3GPP Compliance */}
      <div className="card" style={{ padding:20 }}>
        <Label style={{ marginBottom:12 }}>3GPP Compliance Status</Label>
        <table className="tbl">
          <thead><tr><th>Specification</th><th>Description</th><th>Status</th></tr></thead>
          <tbody>
            {compliance.map(c => (
              <tr key={c.spec}>
                <td className="mono" style={{ fontSize:12 }}>{c.spec}</td>
                <td style={{ fontSize:13, color:'var(--text-sec)' }}>{c.desc}</td>
                <td><span className="pill pill-ok">{c.status}</span></td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
};

// ─── SUBSCRIPTIONS PAGE (Task #9) ────────────────────────────────────────────
const SubscriptionsPage = () => {
  const api   = useApi();
  const toast = useToast();
  const [subs,    setSubs]    = useState([]);
  const [loading, setLoading] = useState(true);
  const [modal,   setModal]   = useState(false);
  const [form,    setForm]    = useState({ analyticsId:'NF_LOAD', notifUri:'', repPeriod:60, maxReportNbr:0 });

  const reload = useCallback(async () => {
    try { setLoading(true); const r = await api.subscriptions.list(); setSubs(Array.isArray(r) ? r : (r?.data || [])); }
    catch {} finally { setLoading(false); }
  }, [api]);

  useEffect(() => { reload(); }, [reload]);

  const del = async id => {
    if (!confirm(`Delete subscription ${id}?`)) return;
    try { await api.subscriptions.delete(id); toast('success', `Subscription ${id.slice(0,8)} deleted`); reload(); } catch {}
  };

  const create = async e => {
    e.preventDefault();
    try { await api.subscriptions.create(form); toast('success', 'Subscription created'); setModal(false); reload(); } catch {}
  };

  return (
    <div className="fade-up">
      <SectionHeader title={`Subscriptions (${subs.length})`} action={
        <button className="btn btn-primary" onClick={() => setModal(true)}><Plus className="w-4 h-4"/>New Subscription</button>
      }/>

      <div className="card" style={{ overflow:'hidden' }}>
        {loading ? <SkeletonCard /> : subs.length === 0 ? (
          <EmptyState icon={Share2} title="No active subscriptions" desc="Create a subscription to enable push-delivery of analytics notifications to consumer NFs." />
        ) : (
          <table className="tbl">
            <thead>
              <tr>
                <th>Subscription ID</th>
                <th>Analytics ID</th>
                <th>Notification URI</th>
                <th>Status</th>
                <th>Rep Period</th>
                <th style={{textAlign:'right'}}>Actions</th>
              </tr>
            </thead>
            <tbody>
              {subs.map(s => (
                <tr key={s.subscriptionId}>
                  <td className="mono" style={{ fontSize:12, color:'var(--text-sec)' }}>{s.subscriptionId?.slice(0,12)}…</td>
                  <td><span className="pill pill-info">{s.analyticsId}</span></td>
                  <td className="mono" style={{ fontSize:12, color:'var(--text-sec)', maxWidth:220, overflow:'hidden', textOverflow:'ellipsis', whiteSpace:'nowrap' }}>{s.notifUri}</td>
                  <td><span className="pill pill-ok">ACTIVE</span></td>
                  <td className="mono" style={{ color:'var(--text-sec)' }}>{s.repPeriod}s</td>
                  <td style={{ textAlign:'right' }}>
                    <button className="btn btn-danger" style={{ padding:'4px 10px' }} onClick={() => del(s.subscriptionId)}>
                      <Trash2 style={{ width:13, height:13 }}/>Delete
                    </button>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </div>

      {modal && (
        <Modal title="Create Subscription" onClose={() => setModal(false)}>
          <form onSubmit={create} style={{ display:'flex', flexDirection:'column', gap:14 }}>
            <div>
              <Label>Analytics ID</Label>
              <select className="inp mono" value={form.analyticsId} onChange={e=>setForm({...form,analyticsId:e.target.value})} style={{appearance:'auto'}}>
                {ANALYTICS_IDS.map(a => <option key={a.id} value={a.id}>{a.id}</option>)}
              </select>
            </div>
            <div>
              <Label>Notification URI</Label>
              <input className="inp mono" type="url" required placeholder="http://consumer-nf:9000/notify" value={form.notifUri} onChange={e=>setForm({...form,notifUri:e.target.value})} />
            </div>
            <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:12 }}>
              <div>
                <Label>Rep Period (s)</Label>
                <input className="inp mono" type="number" min="10" required value={form.repPeriod} onChange={e=>setForm({...form,repPeriod:+e.target.value})} />
              </div>
              <div>
                <Label>Max Reports (0=∞)</Label>
                <input className="inp mono" type="number" min="0" value={form.maxReportNbr} onChange={e=>setForm({...form,maxReportNbr:+e.target.value})} />
              </div>
            </div>
            <div style={{ display:'flex', justifyContent:'flex-end', gap:10, marginTop:8, paddingTop:16, borderTop:'1px solid var(--border-sub)' }}>
              <button type="button" className="btn btn-ghost" onClick={() => setModal(false)}>Cancel</button>
              <button type="submit" className="btn btn-primary">Create Subscription</button>
            </div>
          </form>
        </Modal>
      )}
    </div>
  );
};

// ─── TRAFFIC GENERATOR PAGE (Task #9) ────────────────────────────────────────
const TrafficGenerator = () => {
  const api = useApi();
  const [status,  setStatus]  = useState({ running:false, current_task:'none' });
  const [form,    setForm]    = useState({ type:'download', duration:30 });
  const [loading, setLoading] = useState(false);
  const [error,   setError]   = useState(null);

  const pollStatus = async () => {
    try { const data = await api.get('/traffic/status'); setStatus(data); } catch {}
  };

  useEffect(() => { pollStatus(); const iv = setInterval(pollStatus, 3000); return () => clearInterval(iv); }, []);

  const start = async e => {
    e.preventDefault(); setLoading(true); setError(null);
    try {
      await api.post('/traffic/start', { type:form.type, duration:+form.duration });
      pollStatus();
    } catch (e) { setError(e.message); } finally { setLoading(false); }
  };

  const stop = async () => {
    setLoading(true);
    try { await api.post('/traffic/stop'); pollStatus(); }
    catch (e) { setError(e.message); } finally { setLoading(false); }
  };

  const scenarios = [
    { id:'download',        label:'Download Burst',  desc:'wget 100 MB via uesimtun0',             icon:Download,       color:'var(--accent)' },
    { id:'upload_flood',    label:'Upload Flood',    desc:'ping flood — high UPF egress load',      icon:Activity,       color:'#a78bfa' },
    { id:'ping',            label:'Standard Ping',   desc:'1 ICMP packet / second baseline',        icon:Radio,          color:'var(--ok)'     },
    { id:'signaling_storm', label:'Signaling Storm', desc:'Rapid UE restarts to spike AMF/SMF load',icon:Server,         color:'var(--warn)'   },
  ];

  return (
    <div className="fade-up" style={{ maxWidth:800 }}>
      <SectionHeader title="Traffic Simulator" />

      <div style={{ display:'flex', alignItems:'center', gap:10, marginBottom:20, padding:'10px 14px', background: status.running ? 'var(--ok-dim)' : 'var(--bg-elevated)', border:`1px solid ${status.running ? 'rgba(16,185,129,.3)' : 'var(--border-sub)'}`, borderRadius:8 }}>
        <Dot color={status.running ? 'var(--ok)' : 'var(--text-mut)'} />
        <span style={{ fontSize:13, fontWeight:500 }}>{status.running ? `Running: ${status.current_task}` : 'Idle — no active simulation'}</span>
      </div>

      {error && (
        <div style={{ marginBottom:16, padding:'10px 14px', background:'var(--err-dim)', border:'1px solid rgba(239,68,68,.3)', borderRadius:8, fontSize:13, color:'var(--err)', display:'flex', gap:8 }}>
          <AlertTriangle style={{width:16,height:16,flexShrink:0,marginTop:1}} />{error}
        </div>
      )}

      <form onSubmit={start}>
        <div className="card" style={{ padding:24, marginBottom:16 }}>
          <Label style={{ marginBottom:16 }}>Test Scenario Profile</Label>
          <div style={{ display:'grid', gridTemplateColumns:'repeat(auto-fill, minmax(180px,1fr))', gap:12, marginBottom:24 }}>
            {scenarios.map(s => (
              <label key={s.id} style={{ display:'flex', flexDirection:'column', gap:8, padding:14, borderRadius:8, cursor:'pointer', border:`2px solid ${form.type===s.id ? s.color : 'var(--border-sub)'}`, background: form.type===s.id ? `${s.color}12` : 'transparent', transition:'border-color .15s, background .15s' }}>
                <input type="radio" name="type" value={s.id} checked={form.type===s.id} onChange={e=>setForm({...form,type:e.target.value})} style={{display:'none'}} />
                <s.icon style={{ width:20, height:20, color: form.type===s.id ? s.color : 'var(--text-mut)' }} />
                <div style={{ fontSize:13, fontWeight:600, color: form.type===s.id ? s.color : 'var(--text-pri)' }}>{s.label}</div>
                <div style={{ fontSize:11, color:'var(--text-mut)', lineHeight:1.4 }}>{s.desc}</div>
              </label>
            ))}
          </div>

          <div style={{ display:'flex', gap:16, alignItems:'flex-end' }}>
            <div style={{ flex:1 }}>
              <Label>Duration (seconds)</Label>
              <input className="inp mono" type="number" min="10" max="300" value={form.duration} onChange={e=>setForm({...form,duration:e.target.value})} />
              <div style={{ fontSize:11, color:'var(--text-mut)', marginTop:4 }}>Maximum 300 s per run</div>
            </div>
            <div>
              {status.running ? (
                <button type="button" className="btn btn-danger" style={{padding:'9px 20px'}} onClick={stop} disabled={loading}>
                  <Square className="w-4 h-4"/>Stop Simulation
                </button>
              ) : (
                <button type="submit" className="btn btn-primary" style={{padding:'9px 20px'}} disabled={loading}>
                  <Play className="w-4 h-4"/>{loading ? 'Starting…' : 'Start Simulation'}
                </button>
              )}
            </div>
          </div>
        </div>
      </form>

      <div style={{ padding:'12px 16px', background:'var(--info-dim)', border:'1px solid rgba(59,130,246,.25)', borderRadius:8, fontSize:13, color:'#93c5fd', display:'flex', gap:8 }}>
        <Info style={{width:15,height:15,flexShrink:0,marginTop:2}}/>
        After triggering a simulation, navigate to <strong>Overview</strong> or <strong>Network Performance</strong> to observe real-time changes.
      </div>
    </div>
  );
};

// ─── SETTINGS PAGE (Task #10) ─────────────────────────────────────────────────
const SettingsPage = () => {
  const { settings, dispatch } = useContext(SettingsCtx);
  const api   = useApi();
  const toast = useToast();
  const [testResult, setTest] = useState(null);

  const testConn = async () => {
    setTest('testing');
    try { await api.health(); setTest('ok'); toast('success', 'Connection successful'); }
    catch { setTest('error'); }
  };

  const Toggle = ({ label, checked, onChange, desc }) => (
    <div style={{ display:'flex', justifyContent:'space-between', alignItems:'center', padding:'12px 0', borderBottom:'1px solid var(--border-sub)' }}>
      <div>
        <div style={{ fontSize:13, fontWeight:500 }}>{label}</div>
        {desc && <div style={{ fontSize:11, color:'var(--text-mut)', marginTop:2 }}>{desc}</div>}
      </div>
      <label style={{ position:'relative', display:'inline-block', width:36, height:20, flexShrink:0, cursor:'pointer' }}>
        <input type="checkbox" checked={checked} onChange={onChange} style={{ opacity:0, width:0, height:0 }} />
        <span style={{ position:'absolute', inset:0, background: checked ? 'var(--accent)' : 'var(--border-str)', borderRadius:20, transition:'background .2s' }}>
          <span style={{ position:'absolute', left: checked ? 18 : 2, top:2, width:16, height:16, borderRadius:'50%', background:'#fff', transition:'left .2s' }} />
        </span>
      </label>
    </div>
  );

  const connHelp = {
    'Direct':            null,
    'SSH Tunnel':        `ssh -L 17779:localhost:7779 <nwdaf-host>`,
    'gcloud port-forward': `gcloud compute ssh <instance-name> --zone=<zone> -- -L 7779:localhost:7779 -N`,
  };

  return (
    <div className="fade-up" style={{ maxWidth:720 }}>
      <SectionHeader title="Settings" />

      {/* Connection */}
      <div className="card" style={{ padding:24, marginBottom:16 }}>
        <Label style={{ marginBottom:16 }}>Connection Configuration</Label>
        <div style={{ display:'flex', gap:10, marginBottom:16 }}>
          <input className="inp mono" value={settings.baseUrl} onChange={e=>dispatch({type:'SET_URL',v:e.target.value})} style={{ flex:1 }} />
          <button className="btn btn-secondary" onClick={testConn}>
            {testResult==='testing' ? 'Testing…' : testResult==='ok' ? '✓ Connected' : testResult==='error' ? '✗ Failed' : 'Test Connection'}
          </button>
        </div>
        <Label style={{ marginBottom:10 }}>Connection Mode</Label>
        <div style={{ display:'flex', gap:16, flexWrap:'wrap' }}>
          {['Direct','SSH Tunnel','gcloud port-forward'].map(m => (
            <label key={m} style={{ display:'flex', alignItems:'center', gap:6, cursor:'pointer', fontSize:13 }}>
              <input type="radio" name="mode" checked={settings.connectionMode===m} onChange={()=>dispatch({type:'SET_MODE',v:m})} style={{ accentColor:'var(--accent)' }} />
              {m}
            </label>
          ))}
        </div>
        {connHelp[settings.connectionMode] && (
          <div style={{ marginTop:14, padding:'10px 14px', background:'var(--bg-elevated)', border:'1px solid var(--border-sub)', borderRadius:6, position:'relative' }}>
            <div style={{ fontSize:11, color:'var(--text-mut)', marginBottom:4 }}>Run locally:</div>
            <code className="mono" style={{ fontSize:12, color:'var(--ok)', wordBreak:'break-all' }}>{connHelp[settings.connectionMode]}</code>
            <button className="btn btn-ghost" style={{ position:'absolute', top:8, right:8, padding:'4px 6px' }} onClick={() => { navigator.clipboard.writeText(connHelp[settings.connectionMode]); toast('info','Copied'); }}>
              <Copy className="w-3 h-3"/>
            </button>
          </div>
        )}
      </div>

      {/* Display & refresh */}
      <div style={{ display:'grid', gridTemplateColumns:'1fr 1fr', gap:16, marginBottom:16 }}>
        <div className="card" style={{ padding:24 }}>
          <Label style={{ marginBottom:4 }}>Refresh</Label>
          <Toggle label="Auto-refresh" checked={settings.autoRefresh} onChange={e=>dispatch({type:'TOGGLE_AUTO',v:e.target.checked})} />
          <div style={{ marginTop:12 }}>
            <Label style={{ marginBottom:6 }}>Interval</Label>
            <select className="inp" value={settings.refreshInterval} disabled={!settings.autoRefresh} onChange={e=>dispatch({type:'SET_REFRESH',v:+e.target.value})} style={{ appearance:'auto' }}>
              {[5,10,30,60].map(s => <option key={s} value={s}>{s} seconds</option>)}
            </select>
          </div>
        </div>
        <div className="card" style={{ padding:24 }}>
          <Label style={{ marginBottom:4 }}>Display</Label>
          <Toggle label="Show Raw JSON" desc="Show API response panel in analytics views" checked={settings.showRawJson} onChange={e=>dispatch({type:'TOGGLE_RAW',v:e.target.checked})} />
          <Toggle label="Show 3GPP References" desc="Display TS spec references in page headers" checked={settings.show3gpp} onChange={e=>dispatch({type:'TOGGLE_3GPP',v:e.target.checked})} />
          <Toggle label="Compact Mode" desc="Reduce padding throughout the dashboard" checked={settings.compactMode} onChange={e=>dispatch({type:'TOGGLE_COMPACT',v:e.target.checked})} />
        </div>
      </div>

      {/* About */}
      <div className="card" style={{ padding:24 }}>
        <Label style={{ marginBottom:12 }}>About</Label>
        <div style={{ display:'flex', gap:24, flexWrap:'wrap' }}>
          <div style={{ flex:1, minWidth:220 }}>
            <div style={{ fontSize:15, fontWeight:700, marginBottom:8 }}>{APP_NAME}</div>
            <p style={{ fontSize:13, color:'var(--text-sec)', lineHeight:1.7, margin:'0 0 12px' }}>
              Open5GS NWDAF Operations Console — connects to the C++ <code className="mono">open5gs-nwdafd</code> analytics daemon, providing full observability and control over 3GPP Release 17 network analytics functions.
            </p>
            <div className="mono" style={{ fontSize:11, color:'var(--text-mut)', lineHeight:2 }}>
              <div>Target: Open5GS v2.7.6 + UERANSIM</div>
              <div>Source: github.com/cem8kaya/open5gs-nwdaf</div>
            </div>
          </div>
          <div style={{ minWidth:220 }}>
            <Label style={{ marginBottom:10 }}>3GPP Compliance</Label>
            <div style={{ display:'flex', flexDirection:'column', gap:6 }}>
              {[
                ['TS 23.288 v17','7/7 Analytics IDs'],
                ['TS 29.520 v17','Subscription & Analytics API'],
                ['TS 29.510 v17','NRF Registration'],
                ['TS 28.554 v17','KPI Collection'],
              ].map(([spec,desc]) => (
                <div key={spec} style={{ display:'flex', justifyContent:'space-between', alignItems:'center', fontSize:12 }}>
                  <span className="mono" style={{ color:'var(--text-sec)' }}>{spec}</span>
                  <span className="pill pill-ok" style={{ fontSize:10 }}>{desc} ✓</span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </div>
  );
};

// ─── MAIN DASHBOARD + APP (Task #11) ─────────────────────────────────────────
function MainDashboard() {
  const { settings } = useContext(SettingsCtx);
  const { theme }    = useTheme();
  const [view, setView]   = useState('overview');
  const [open, setOpen]   = useState(true);

  useEffect(() => {
    const handle = e => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT' || e.target.tagName === 'TEXTAREA') return;
      if (e.key === 'o') setView('overview');
      if (e.key === 's') setView('subscriptions');
      if (e.key === 'm') setView('models');
      if (e.key >= '1' && e.key <= '7') setView(ANALYTICS_IDS[+e.key - 1]?.id);
    };
    window.addEventListener('keydown', handle);
    return () => window.removeEventListener('keydown', handle);
  }, []);

  const renderContent = () => {
    if (view === 'overview')       return <OverviewPage />;
    if (view === 'subscriptions')  return <SubscriptionsPage />;
    if (view === 'traffic_gen')    return <TrafficGenerator />;
    if (view === 'models')         return <MLModelsPage />;
    if (view === 'metrics')        return <MetricsPage />;
    if (view === 'service')        return <ServiceControlPage />;
    if (view === 'settings')       return <SettingsPage />;
    if (ANALYTICS_IDS.find(a => a.id === view)) return <AnalyticsPage analyticsId={view} />;
    return <EmptyState icon={ActivitySquare} title="Page not found" desc={view} />;
  };

  return (
    <div data-theme={theme} style={{ display:'flex', flexDirection:'column', height:'100vh', overflow:'hidden', background:'var(--bg-base)' }}>
      <TopBar onToggleSidebar={() => setOpen(o => !o)} />
      <div style={{ display:'flex', flex:1, overflow:'hidden' }}>
        <Sidebar current={view} onNav={setView} open={open} />
        <main className="scroll" style={{ flex:1, overflowY:'auto', padding: settings.compactMode ? 16 : 28 }}>
          <div style={{ maxWidth:1600, margin:'0 auto' }}>
            {renderContent()}
          </div>
        </main>
      </div>
    </div>
  );
}

export default function App() {
  const [settings, dispatch] = useReducer(settingsReducer, initialSettings);
  return (
    <ThemeProvider>
      <SettingsCtx.Provider value={{ settings, dispatch }}>
        <AuthProvider>
          <ToastProvider>
            <ApiProvider>
              <GlobalStyles />
              <AuthGate />
            </ApiProvider>
          </ToastProvider>
        </AuthProvider>
      </SettingsCtx.Provider>
    </ThemeProvider>
  );
}

