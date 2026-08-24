// Translate Chinese UI/runtime text to English in the 5 listed DSH plugin files.
// Exact string replacement, longest-segment-first, UTF-8 without BOM.
// Each pair: [original, replacement, expectedOccurrenceCount]
import { readFileSync, writeFileSync } from "node:fs";

const BASE = "C:\\Users\\me\\.dsh\\profiles\\desktop\\node_modules";

// ---------------------------------------------------------------- shared defaults
// The same default texts appear in dsh-client-auto-continue/lib/client.js (DEFAULT_CONFIG)
// and lib/index.js (schema defaults). Keep translations identical across both files.
const SHARED_DEFAULTS = [
	["\"继续\"", "\"Continue\"", 2],
	["\"(上一步工具「{tool}」可能未完成, 先确认状态再继续, 不要重复执行)\"", "\"(The previous tool call {tool} may not have completed; verify its status before continuing, and do not repeat it.)\"", 1],
	["\"(上一步工具「{tool}」已完成, 结果: {result}; 不要重复执行, 直接继续)\"", "\"(The previous tool call {tool} has completed; result: {result}. Do not repeat it — continue directly.)\"", 1],
	["\"(检测到你可能陷入循环, 请停止重复刚才的动作, 换一种方式继续)\"", "\"(It looks like you may be stuck in a loop — stop repeating what you just did and try a different approach.)\"", 1]
];

// ---------------------------------------------------------------- dsh-client-auto-continue/lib/client.js
const AUTO_CONTINUE_CLIENT = `${BASE}\\dsh-client-auto-continue\\lib\\client.js`;
const AUTO_CONTINUE_CLIENT_PAIRS = [
	...SHARED_DEFAULTS,
	["  /** 会话标题缓存(来自 session.list 投影, {sessionTitle} 占位符用)。 */", "  /** Session-title cache (from the session.list projection, for the {sessionTitle} placeholder). */", 1],
	["`已启动(文本=\"${config.continueText}\", 宽限 ${config.graceMs}ms, 冷却 ${config.cooldownMs}ms, 最多连续 ${config.maxConsecutive} 次)`", "`started (text=\"${config.continueText}\", grace ${config.graceMs}ms, cooldown ${config.cooldownMs}ms, max consecutive ${config.maxConsecutive} sends)`", 1],
	["  // ---------- mux 帧 ----------", "  // ---------- mux frames ----------", 1],
	["\"出现排队消息\"", "\"queued message arrived\"", 1],
	["  /** 从 assistant/message 事件提取纯文本。 */", "  /** Extract plain text from an assistant/message event. */", 1],
	[
		"  /**\n   * loop guard 信号 1(空转): 时间窗内连续短句且期间无工具调用。\n   * 短句 = 模型消息文本短于 loopShortChars; 长句、工具调用、或短句间隔超过\n   * loopWindowMs(正常思考的短文本散布在长时间里)都会重置计数。\n   */",
		"  /**\n   * Loop guard signal 1 (spinning): consecutive short sentences inside the\n   * window with no tool call in between. Short sentence = model message text\n   * shorter than loopShortChars; long sentences, tool calls, or short\n   * sentences spread past loopWindowMs (normal thinking) reset the count.\n   */",
		1
	],
	["  /** 两个循环信号的公共检查; 命中且本回合未打断过则打断。 */", "  /** Shared check for both loop signals; interrupt when hit and not already interrupted this turn. */", 1],
	["`检测到空转循环 ${sessionId}: 连续 ${state.sameTextRun} 条相同消息`", "`detected spinning loop ${sessionId}: ${state.sameTextRun} consecutive identical messages`", 1],
	["`检测到空转循环 ${sessionId}: 连续 ${state.shortRun} 条短句且无工具调用`", "`detected spinning loop ${sessionId}: ${state.shortRun} consecutive short sentences with no tool calls`", 1],
	["`检测到工具死循环 ${sessionId}: 「${toolName}」连续 ${state.toolRun.count} 次(同参数同结果)`", "`detected tool loop ${sessionId}: \"${toolName}\" ran ${state.toolRun.count} times (same args, same result)`", 1],
	[
		"  /**\n   * 打断运行中的回合: cancel(带来源标记)+ 进冷却。\n   * 随后的 turn/end aborted 会因 loopCancelled 走「可恢复中断」路径,\n   * 用 loopText 重启回合——不会与用户手动停止混淆。\n   */",
		"  /**\n   * Interrupt the running turn: cancel (with a source marker) and enter cooldown.\n   * The later turn/end aborted takes the \"recoverable interruption\" path\n   * because of loopCancelled, restarting the turn with loopText — never\n   * confused with a manual user stop.\n   */",
		1
	],
	["`跳过循环打断 ${sessionId}: 处于冷却期`", "`skipped loop interrupt ${sessionId}: in cooldown`", 1],
	["`已打断循环 ${sessionId}: ${response.result.ok ? \"cancel 已受理\" : \"cancel 被拒绝\"}`", "`interrupted loop ${sessionId}: ${response.result.ok ? \"cancel accepted\" : \"cancel rejected\"}`", 1],
	["`打断循环失败 ${sessionId}: ${error instanceof Error ? error.message : String(error)}`", "`loop interrupt failed ${sessionId}: ${error instanceof Error ? error.message : String(error)}`", 1],
	["\"宿主自行开启新回合\"", "\"host started a new turn on its own\"", 1],
	["\"收到新的 turn/end\"", "\"new turn/end received\"", 1],
	["\"用户手动发送消息\"", "\"user sent a message manually\"", 1],
	["  // ---------- host 帧 ----------", "  // ---------- host frames ----------", 1],
	["\"宿主报告会话开始运行\"", "\"host reported the session running\"", 1],
	["`跳过 ${frame.sessionId}: 永久性 agent 错误 — ${frame.message}`", "`skipped ${frame.sessionId}: permanent agent error — ${frame.message}`", 1],
	["\"dsh-auto-continue: 未自动继续\"", "\"dsh-auto-continue: did not auto-continue\"", 2],
	["`${frame.sessionId}: 永久性 agent 错误 ${frame.message.slice(0, 120)}`", "`${frame.sessionId}: permanent agent error ${frame.message.slice(0, 120)}`", 1],
	["\"会话已移除\"", "\"session removed\"", 1],
	["  // ---------- 调度 ----------", "  // ---------- scheduling ----------", 1],
	["  /** 回合失败入口: 先做错误分类, 永久性失败跳过并通知, 临时性失败走正常调度。 */", "  /** Turn-failure entry: classify first — permanent failures are skipped and notified; transient ones take the normal schedule. */", 1],
	["`跳过 ${sessionId}(${reason}): 永久性失败 ${summary} — ${failure.message}`", "`skipped ${sessionId}(${reason}): permanent failure ${summary} — ${failure.message}`", 1],
	["`${sessionId}: 永久性错误 ${summary}，需要人工处理`", "`${sessionId}: permanent error ${summary} — manual intervention required`", 1],
	["  /** 通知操作按钮与回调(「立即续跑」/「暂停该会话 1 小时」)。 */", "  /** Notification action buttons and callback (\"Resume now\" / \"Pause this session 1h\"). */", 1],
	["\"立即续跑\"", "\"Resume now\"", 1],
	["\"暂停该会话 1 小时\"", "\"Pause this session 1h\"", 1],
	["`通知按钮: 立即续跑 ${sessionId}`", "`notification button: resume now ${sessionId}`", 1],
	["`通知按钮: 暂停 ${sessionId} 1 小时`", "`notification button: pause ${sessionId} for 1h`", 1],
	["\"通知按钮暂停该会话\"", "\"notification button paused the session\"", 1],
	["  /** 恢复结果记账: 自动发送后窗口内的回合结束, 判定恢复成功或失败。 */", "  /** Recovery bookkeeping: a turn ending inside the post-send window counts as a recovered or failed resume. */", 1],
	["`恢复结果(${sessionId}): ${outcome === \"completed\" ? \"成功\" : \"失败\"}`", "`recovery result(${sessionId}): ${outcome === \"completed\" ? \"succeeded\" : \"failed\"}`", 1],
	["  /** 立即为该会话发送一次自动继续(无视冷却与连续上限; 由通知按钮触发)。 */", "  /** Immediately send one auto-continue for the session (ignoring cooldown and the consecutive cap; triggered by the notification button). */", 1],
	["  /** 本会话当前生效的冷却间隔(自适应退避)。 */", "  /** The currently effective cooldown interval for this session (adaptive backoff). */", 1],
	["`跳过 ${sessionId}(${reason}): 全局暂停中`", "`skipped ${sessionId}(${reason}): globally paused`", 1],
	["`跳过 ${sessionId}(${reason}): 会话暂停中`", "`skipped ${sessionId}(${reason}): session paused`", 1],
	["`跳过 ${sessionId}(${reason}): 已连续自动继续 ${state.consecutive} 次, 等待用户介入或成功回合`", "`skipped ${sessionId}(${reason}): already auto-continued ${state.consecutive} times; waiting for user intervention or a completed turn`", 1],
	["`检测到非人为中断 ${sessionId}(${reason}), ${config.graceMs}ms 后自动发送「${template}」`", "`detected non-human interruption ${sessionId}(${reason}); sending \"${template}\" after ${config.graceMs}ms`", 1],
	["`取消 ${sessionId} 的自动继续(${why})`", "`cancelled auto-continue for ${sessionId} (${why})`", 1],
	["`跳过 ${sessionId}: 无法确认空闲(${running === void 0 ? \"未知\" : \"运行中\"})`", "`skipped ${sessionId}: cannot confirm idle (${running === void 0 ? \"unknown\" : \"running\"})`", 1],
	["`跳过 ${sessionId}: 会话仍在运行`", "`skipped ${sessionId}: session still running`", 1],
	["`跳过 ${sessionId}: 已有排队消息`", "`skipped ${sessionId}: messages already queued`", 1],
	["`跳过 ${sessionId}: 其他标签页刚发送过`", "`skipped ${sessionId}: another tab sent recently`", 1],
	["`跳过 ${sessionId}: 其他标签页正在发送`", "`skipped ${sessionId}: another tab is sending`", 1],
	["`已自动发送「${text}」到 ${sessionId}(${reason}), 第 ${state.consecutive} 次连续`", "`auto-sent \"${text}\" to ${sessionId}(${reason}), consecutive #${state.consecutive}`", 1],
	["\"dsh-auto-continue: 已自动继续\"", "\"dsh-auto-continue: auto-continued\"", 1],
	["`${sessionId}: 已发送「${text}」(第 ${state.consecutive} 次连续)`", "`${sessionId}: sent \"${text}\" (consecutive #${state.consecutive})`", 1],
	["`达到连续上限 ${config.maxConsecutive} 次, 停止自动继续 ${sessionId}`", "`reached ${config.maxConsecutive} consecutive limit, stopped auto-continue for ${sessionId}`", 1],
	["\"dsh-auto-continue: 已停止自动继续\"", "\"dsh-auto-continue: auto-continue stopped\"", 1],
	["`${sessionId}: 连续失败 ${state.consecutive} 次, 需要人工介入`", "`${sessionId}: failed ${state.consecutive} times consecutively — manual intervention needed`", 1],
	["`发送失败 ${sessionId}: ${response.result.error.code} ${response.result.error.message}`", "`send failed ${sessionId}: ${response.result.error.code} ${response.result.error.message}`", 1],
	["`发送异常 ${sessionId}: ${error instanceof Error ? error.message : String(error)}`", "`send error ${sessionId}: ${error instanceof Error ? error.message : String(error)}`", 1],
	[
		"  /**\n   * 组装本次续跑消息: 模板填充 + 幂等护栏。\n   * 护栏依据上一步工具调用的执行状态附加指引, 防止重跑副作用操作:\n   * - 结果未确认(可能已部分执行)→ 提示先确认状态、不要重复执行\n   * - 已确认成功 → 提示已完成、不要重复执行\n   * - 已失败 → 不加护栏(重试工具本来就是目的)\n   */",
		"  /**\n   * Assemble the continuation message: template fill + idempotency guard.\n   * The guard appends guidance based on the last tool call's execution state\n   * to prevent rerunning side-effecting operations:\n   * - result unconfirmed (may have partially executed) → check state first, don't repeat\n   * - confirmed success → note it is done, don't repeat\n   * - failed → no guard (retrying the tool is the point)\n   */",
		1
	],
	["  /** 上一步工具调用的护栏状态(实时路径, 由 mux 帧维护)。 */", "  /** Guard state of the last tool call (live path, maintained by mux frames). */", 1],
	["  /** 查一次 session.list, 顺带缓存该会话的标题。 */", "  /** Query session.list once, caching the session's title along the way. */", 1],
	["  // ---------- 启动/重连扫描 ----------", "  // ---------- boot / reconnect scan ----------", 1],
	["  /** 反复尝试扫描, 直到成功(宿主就绪)或达到次数上限。 */", "  /** Keep retrying the scan until it succeeds (host ready) or the attempt cap is reached. */", 1],
	[
		"  /**\n   * 扫描最近中断过的会话: 最后回合以非人为原因结束, 且其后没有新回合或用户消息。\n   * @returns 是否成功完成一次扫描(宿主就绪)。\n   */",
		"  /**\n   * Scan sessions interrupted recently: the last turn ended with a non-human\n   * reason and no new turn or user message followed it.\n   * @returns whether one scan completed successfully (host ready).\n   */",
		1
	],
	["`扫描发现中断 ${summary.sessionId}(turn/end:${reason.kind}), 安排自动继续`", "`scan found interruption ${summary.sessionId}(turn/end:${reason.kind}), scheduling auto-continue`", 1],
	["  /** 从历史事件恢复上一步工具调用状态(扫描路径的幂等护栏)。 */", "  /** Restore the last tool call state from history events (idempotency guard for the scan path). */", 1],
	// ---- locale zh map values (keys kept) ----
	["\"card.title\": \"自动继续\"", "\"card.title\": \"Auto continue\"", 1],
	["\"card.description\": \"请求因网络等原因(非人为)中断后, 自动发送「继续」续跑。\"", "\"card.description\": \"When a request is interrupted by a non-human cause (e.g. network), automatically send \\\"Continue\\\" to resume.\"", 1],
	["\"field.paused\": \"暂停自动继续\"", "\"field.paused\": \"Pause auto-continue\"", 1],
	["\"field.pausedHint\": \"全局暂停: 实时与扫描都不会再自动发送, 已排队的待发送也会取消。\"", "\"field.pausedHint\": \"Globally pause: no live or scan auto-send fires, and queued pending sends are cancelled.\"", 1],
	["\"field.continueText\": \"继续文本\"", "\"field.continueText\": \"Continue text\"", 1],
	["\"field.continueTextHint\": \"中断后自动发送的消息内容。\"", "\"field.continueTextHint\": \"Message automatically sent after an interruption.\"", 1],
	["\"field.continueTextMaxTokens\": \"超限时的继续文本\"", "\"field.continueTextMaxTokens\": \"Continue text (max tokens)\"", 1],
	["\"field.continueTextMaxTokensHint\": \"达到输出 token 上限时自动发送的文本, 支持与继续文本相同的占位符。\"", "\"field.continueTextMaxTokensHint\": \"Text sent when the output token ceiling is reached; same placeholders as the continue text.\"", 1],
	["\"field.guardTools\": \"幂等护栏\"", "\"field.guardTools\": \"Idempotency guard\"", 1],
	["\"field.guardToolsHint\": \"续跑前检查上一步工具调用: 结果未确认时提示先确认状态, 已成功时提示不要重复执行, 避免重复 commit/调 API。\"", "\"field.guardToolsHint\": \"Before resuming, inspect the last tool call: if its result is unconfirmed, tell the model to check state first; if it succeeded, tell it not to rerun — avoids duplicate commits / API calls.\"", 1],
	["\"field.guardPendingText\": \"结果未确认时的护栏文本\"", "\"field.guardPendingText\": \"Guard text (unconfirmed result)\"", 1],
	["\"field.guardPendingTextHint\": \"上一步工具可能已部分执行时附加到继续文本之后, 支持 {tool} 占位符。\"", "\"field.guardPendingTextHint\": \"Appended when the last tool may have partially executed; supports the {tool} placeholder.\"", 1],
	["\"field.guardDoneText\": \"工具已成功时的护栏文本\"", "\"field.guardDoneText\": \"Guard text (tool succeeded)\"", 1],
	["\"field.guardDoneTextHint\": \"上一步工具已确认成功时附加到继续文本之后, 支持 {tool} 与 {result}(结果摘要)占位符。\"", "\"field.guardDoneTextHint\": \"Appended when the last tool is confirmed done; supports {tool} and {result} (result excerpt) placeholders.\"", 1],
	["\"field.graceMs\": \"宽限期 (ms)\"", "\"field.graceMs\": \"Grace period (ms)\"", 1],
	["\"field.graceMsHint\": \"检测到中断后等待的时长; 期间宿主自行恢复则取消。\"", "\"field.graceMsHint\": \"Wait after an interruption; cancelled if the host recovers on its own.\"", 1],
	["\"field.cooldownMs\": \"冷却时间 (ms)\"", "\"field.cooldownMs\": \"Cooldown (ms)\"", 1],
	["\"field.cooldownMsHint\": \"同一会话两次自动「继续」的最小间隔, 失败尝试也计入。\"", "\"field.cooldownMsHint\": \"Minimum interval between auto-continues per session; failed attempts count too.\"", 1],
	["\"field.maxConsecutive\": \"最大连续次数\"", "\"field.maxConsecutive\": \"Max consecutive\"", 1],
	["\"field.maxConsecutiveHint\": \"同一会话连续自动「继续」的上限; 超过后停止, 直到用户手动介入或出现成功回合。\"", "\"field.maxConsecutiveHint\": \"Max consecutive auto-continues per session; stops until a user intervenes or a turn completes.\"", 1],
	["\"field.scanOnBoot\": \"启动/重连扫描\"", "\"field.scanOnBoot\": \"Scan on load / reconnect\"", 1],
	["\"field.scanOnBootHint\": \"页面启动或重连时扫描最近中断的会话并自动续跑(如浏览器关闭期间宿主崩溃)。\"", "\"field.scanOnBootHint\": \"Scan recently interrupted sessions on page load or reconnect (e.g. the host crashed while the browser was closed).\"", 1],
	["\"field.scanLimit\": \"扫描会话数\"", "\"field.scanLimit\": \"Scan limit\"", 1],
	["\"field.scanLimitHint\": \"最多检查多少个最近更新的会话(不含运行中与子代理会话)。\"", "\"field.scanLimitHint\": \"How many most-recently-updated sessions to check (running / subagent sessions excluded).\"", 1],
	["\"field.freshMs\": \"扫描时间窗 (ms)\"", "\"field.freshMs\": \"Scan window (ms)\"", 1],
	["\"field.freshMsHint\": \"扫描只处理该时间窗内的中断。\"", "\"field.freshMsHint\": \"Only interruptions inside this window are considered.\"", 1],
	["\"field.reconnectScanDelayMs\": \"重连扫描延迟 (ms)\"", "\"field.reconnectScanDelayMs\": \"Reconnect scan delay (ms)\"", 1],
	["\"field.reconnectScanDelayMsHint\": \"重连后等待宿主完成恢复再扫描。\"", "\"field.reconnectScanDelayMsHint\": \"Wait for the host to finish recovering before scanning after a reconnect.\"", 1],
	["\"field.reconnectBackoffMs\": \"重连退避 (ms)\"", "\"field.reconnectBackoffMs\": \"Reconnect backoff (ms)\"", 1],
	["\"field.reconnectBackoffMsHint\": \"事件流断开后的重连间隔。\"", "\"field.reconnectBackoffMsHint\": \"Interval between event-stream reconnect attempts.\"", 1],
	["\"field.verbose\": \"详细日志\"", "\"field.verbose\": \"Verbose logs\"", 1],
	["\"field.verboseHint\": \"在浏览器控制台输出 [auto-continue] 日志。\"", "\"field.verboseHint\": \"Log [auto-continue] lines to the browser console.\"", 1],
	["\"field.classify\": \"错误分类\"", "\"field.classify\": \"Classify errors\"", 1],
	["\"field.classifyHint\": \"仅自动恢复临时性错误(网络/超时/5xx 等); 认证/余额/模型不存在等永久性错误跳过并通知。\"", "\"field.classifyHint\": \"Auto-resume transient failures only (network/timeout/5xx…); auth, balance and model errors are skipped and notified.\"", 1],
	["\"field.backoffFactor\": \"退避系数\"", "\"field.backoffFactor\": \"Backoff factor\"", 1],
	["\"field.backoffFactorHint\": \"连续失败时冷却间隔的倍率(如 2 表示 20s→40s→80s 递增)。\"", "\"field.backoffFactorHint\": \"Cooldown multiplier per consecutive failure (2 = 20s→40s→80s…).\"", 1],
	["\"field.backoffMaxMs\": \"最大退避间隔 (ms)\"", "\"field.backoffMaxMs\": \"Max backoff (ms)\"", 1],
	["\"field.backoffMaxMsHint\": \"自适应退避的上限, 防止等待过久。\"", "\"field.backoffMaxMsHint\": \"Cap on the adaptive backoff interval.\"", 1],
	["\"field.notify\": \"浏览器通知\"", "\"field.notify\": \"Browser notifications\"", 1],
	["\"field.notifyHint\": \"自动继续成功/放弃/遇到永久性错误时弹出浏览器通知, 通知带「立即续跑」与「暂停该会话 1 小时」按钮。\"", "\"field.notifyHint\": \"Notify when auto-continue fires, gives up, or hits a permanent error; notifications carry \\\"Resume now\\\" and \\\"Pause this session 1h\\\" buttons.\"", 1],
	["\"stats.title\": \"今日统计\"", "\"stats.title\": \"Today's stats\"", 1],
	["\"stats.sent\": \"自动继续\"", "\"stats.sent\": \"Auto-continued\"", 1],
	["\"stats.skipped\": \"跳过(永久错误)\"", "\"stats.skipped\": \"Skipped (permanent)\"", 1],
	["\"stats.recovered\": \"恢复成功\"", "\"stats.recovered\": \"Recovered\"", 1],
	["\"stats.failed\": \"继续后仍失败\"", "\"stats.failed\": \"Failed after\"", 1],
	["\"stats.gaveUp\": \"停止(达上限)\"", "\"stats.gaveUp\": \"Gave up (cap)\"", 1],
	["\"stats.looped\": \"循环打断\"", "\"stats.looped\": \"Loops broken\"", 1],
	["\"field.loopGuard\": \"循环守卫\"", "\"field.loopGuard\": \"Loop guard\"", 1],
	["\"field.loopGuardHint\": \"检测运行中的回合空转: 连续短句且无工具调用, 或连续调用相同工具时, 自动取消并用循环提示文本重启回合。\"", "\"field.loopGuardHint\": \"Detects a running turn spinning in place — many short sentences with no tool calls, or the same tool repeating — cancels it and restarts with the loop text.\"", 1],
	["\"field.loopShortChars\": \"短句长度上限 (字符)\"", "\"field.loopShortChars\": \"Short-sentence max (chars)\"", 1],
	["\"field.loopShortCharsHint\": \"模型消息文本短于该值计为一条短句(空转信号)。\"", "\"field.loopShortCharsHint\": \"A model message shorter than this counts as a short sentence (spinning signal).\"", 1],
	["\"field.loopWindowMs\": \"短句时间窗 (ms)\"", "\"field.loopWindowMs\": \"Short-sentence window (ms)\"", 1],
	["\"field.loopWindowMsHint\": \"连续短句必须落在这个时间窗内; 正常思考的短文本散布在长时间里不会被误判。\"", "\"field.loopWindowMsHint\": \"Consecutive short sentences must land inside this window; normal thinking spread over time is not misjudged.\"", 1],
	["\"field.loopShortCount\": \"连续短句阈值\"", "\"field.loopShortCount\": \"Short-sentence threshold\"", 1],
	["\"field.loopShortCountHint\": \"时间窗内连续多少条短句且期间无工具调用时判定空转循环。\"", "\"field.loopShortCountHint\": \"How many consecutive short sentences inside the window, with no tool call, trip the loop guard.\"", 1],
	["\"field.loopRepeatText\": \"相同消息重复次数\"", "\"field.loopRepeatText\": \"Identical message count\"", 1],
	["\"field.loopRepeatTextHint\": \"连续输出多少条完全相同的消息时判定空转(最强信号, 不限长度, 如模型反复说同一句话)。\"", "\"field.loopRepeatTextHint\": \"How many consecutive identical messages trip the guard (strongest signal, any length, e.g. the model repeating the same line).\"", 1],
	["\"field.loopToolRepeat\": \"同工具重复次数\"", "\"field.loopToolRepeat\": \"Same-tool repeat count\"", 1],
	["\"field.loopToolRepeatHint\": \"同工具+同参数+同结果的连续调用多少次时判定死循环; 参数或结果有变化视为有进展。\"", "\"field.loopToolRepeatHint\": \"How many consecutive calls of the same tool with identical arguments and results trip the loop guard; a changed argument or result counts as progress.\"", 1],
	["\"field.loopText\": \"循环提示文本\"", "\"field.loopText\": \"Loop text\"", 1],
	["\"field.loopTextHint\": \"打断后重启回合时发送的文本, 支持 {tool} 占位符。\"", "\"field.loopTextHint\": \"Text sent after the loop guard restarts a turn; supports the {tool} placeholder.\"", 1],
	["\"stats.byCode\": \"按错误码统计\"", "\"stats.byCode\": \"By error code\"", 1],
	["\"stats.empty\": \"今天还没有自动继续记录。\"", "\"stats.empty\": \"No auto-continue activity today.\"", 1],
	["\"stats.reset\": \"清零\"", "\"stats.reset\": \"Reset\"", 1],
	["\"pause.title\": \"已暂停会话\"", "\"pause.title\": \"Paused sessions\"", 1],
	["\"pause.none\": \"没有暂停中的会话。\"", "\"pause.none\": \"No sessions paused.\"", 1],
	["\"pause.clearAll\": \"全部解除\"", "\"pause.clearAll\": \"Clear all\"", 1],
	["\"pause.unpause\": \"解除\"", "\"pause.unpause\": \"Resume\"", 1],
	["\"pause.minutes\": \"分钟\"", "\"pause.minutes\": \"min\"", 1],
	["\"chrome.collapse\": \"收起设置\"", "\"chrome.collapse\": \"Hide settings\"", 1],
	["\"chrome.expand\": \"展开设置\"", "\"chrome.expand\": \"Show settings\"", 1],
	["\"chrome.unsaved\": \"未保存\"", "\"chrome.unsaved\": \"Unsaved\"", 1],
	["\"chrome.readOnly\": \"当前部署的设置只读。\"", "\"chrome.readOnly\": \"This deployment stores settings read-only.\"", 1],
	["\"chrome.saveFailed\": \"部署未接受这些值, 已保留供你修改。\"", "\"chrome.saveFailed\": \"The deployment did not accept these values; they were left for you to correct.\"", 1],
	["\"chrome.discard\": \"放弃\"", "\"chrome.discard\": \"Discard\"", 1],
	["\"chrome.saving\": \"保存中…\"", "\"chrome.saving\": \"Saving…\"", 1],
	["\"chrome.save\": \"保存\"", "\"chrome.save\": \"Save\"", 1],
	["\"chrome.overridden\": \"已覆盖\"", "\"chrome.overridden\": \"Overridden\"", 1],
	["\"chrome.reset\": \"恢复默认\"", "\"chrome.reset\": \"Reset to default\"", 1],
	["\"chrome.invalidNumber\": \"请输入数字, 留空则使用默认值。\"", "\"chrome.invalidNumber\": \"Enter a number, or leave blank to use the default.\"", 1],
	["\"chrome.inherit\": \"继承\"", "\"chrome.inherit\": \"Inherit\"", 1],
	["\"chrome.on\": \"开\"", "\"chrome.on\": \"On\"", 1],
	["\"chrome.off\": \"关\"", "\"chrome.off\": \"Off\"", 1],
	// ---- en map leftover Chinese reference ----
	["send 「继续」 to resume.", "send \\\"Continue\\\" to resume.", 1]
];

// ---------------------------------------------------------------- dsh-client-auto-continue/lib/index.js
const AUTO_CONTINUE_INDEX = `${BASE}\\dsh-client-auto-continue\\lib\\index.js`;
const AUTO_CONTINUE_INDEX_PAIRS = [...SHARED_DEFAULTS];

// ---------------------------------------------------------------- dsh-resume/client.js
const RESUME_CLIENT = `${BASE}\\dsh-resume\\client.js`;
const RESUME_CLIENT_PAIRS = [
	["\"继续\"", "\"Continue\"", 1],
	["\"续接\"", "\"Continue\"", 1],
	["\"model.label\": \"模型\"", "\"model.label\": \"Model\"", 1]
];

// ---------------------------------------------------------------- dsh-resume/lib/index.js
const RESUME_INDEX = `${BASE}\\dsh-resume\\lib\\index.js`;
const RESUME_INDEX_PAIRS = [
	[" * @deepseek-ai/dsh-resume — 中断续接 + 重启自动恢复（web profile 插件）。", " * @deepseek-ai/dsh-resume — interrupted-turn resume + automatic restore on restart (web profile plugin).", 1],
	["so a \"继续\" continues it instead of restarting);", "so a \"Continue\" continues it instead of restarting);", 1],
	["(\"不打扰主代理\")", "(\"does not disturb the main agent\")", 1]
];

// ---------------------------------------------------------------- dsh-resume/lib/patch.js
const RESUME_PATCH = `${BASE}\\dsh-resume\\lib\\patch.js`;
const RESUME_PATCH_PAIRS = [
	["and a \"继续\" resumes it. */", "and a \"Continue\" resumes it. */", 1],
	["\"续接\"", "\"Continue\"", 1]
];

// ---------------------------------------------------------------- apply
const JOBS = [
	{ path: AUTO_CONTINUE_CLIENT, pairs: AUTO_CONTINUE_CLIENT_PAIRS, label: "dsh-client-auto-continue/lib/client.js" },
	{ path: AUTO_CONTINUE_INDEX, pairs: AUTO_CONTINUE_INDEX_PAIRS, label: "dsh-client-auto-continue/lib/index.js" },
	{ path: RESUME_CLIENT, pairs: RESUME_CLIENT_PAIRS, label: "dsh-resume/client.js" },
	{ path: RESUME_INDEX, pairs: RESUME_INDEX_PAIRS, label: "dsh-resume/lib/index.js" },
	{ path: RESUME_PATCH, pairs: RESUME_PATCH_PAIRS, label: "dsh-resume/lib/patch.js" }
];

const CJK = /[\u4E00-\u9FFF\u3400-\u4DBF\uF900-\uFAFF\u3000-\u303F\uFF00-\uFFEF]/;

let failed = false;
for (const job of JOBS) {
	let content;
	try {
		content = readFileSync(job.path, "utf8");
	} catch (error) {
		console.error(`READ FAILED ${job.label}: ${error.message}`);
		failed = true;
		continue;
	}
	if (content.charCodeAt(0) === 0xFEFF) content = content.slice(1);

	// verify occurrence counts against the pristine content
	for (const [orig, , expected] of job.pairs) {
		const count = content.split(orig).length - 1;
		if (count !== expected) {
			console.error(`COUNT MISMATCH in ${job.label}: expected ${expected} got ${count} for: ${JSON.stringify(orig.slice(0, 90))}`);
			failed = true;
		}
	}
	if (failed) continue;

	// apply longest-first
	const sorted = [...job.pairs].sort((a, b) => b[0].length - a[0].length);
	let out = content;
	let total = 0;
	for (const [orig, repl] of sorted) {
		const parts = out.split(orig);
		const count = parts.length - 1;
		if (count === 0) {
			console.error(`NOT FOUND after earlier replacements in ${job.label}: ${JSON.stringify(orig.slice(0, 90))}`);
			failed = true;
			continue;
		}
		out = parts.join(repl);
		total += count;
	}

	if (failed) continue;

	const remaining = (out.match(CJK) ?? []).length;
	writeFileSync(job.path, out, "utf8");
	console.log(`OK ${job.label}: ${total} replacements applied, remaining CJK chars: ${remaining}`);
}

if (failed) {
	console.error("One or more jobs failed — no files were written for failed jobs (successful jobs above were written).");
	process.exit(1);
} else {
	console.log("ALL DONE — all 5 files translated and written as UTF-8 (no BOM).");
}
