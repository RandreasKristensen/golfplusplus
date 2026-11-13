#pragma once
// SDK row lookup/cleanup; expired rows do not replay or impose a cooldown.
std::vector<dap_row> daps_of(ReducerContext& ctx, const std::uint64_t account) {
    std::vector<dap_row> rows;
    for (const auto& row : ctx.db[dap_pair_first].filter(account)) { rows.push_back(row); }
    for (const auto& row : ctx.db[dap_pair_second].filter(account)) { rows.push_back(row); }
    return rows;
}
bool dap_busy(ReducerContext& ctx, const std::uint64_t account) {
    for (const auto& row : daps_of(ctx, account)) {
        if (dap_running(row.started_at, ctx.timestamp.micros_since_epoch(), cached_content()->tuning.dap)) { return true; }
    }
    return false;
}
void erase_daps(ReducerContext& ctx, const std::uint64_t account) {
    for (const auto& row : daps_of(ctx, account)) { ctx.db[dap_pair_id].delete_by_key(row.id); }
}
