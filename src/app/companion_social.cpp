#include "launcher.hpp"
#include <algorithm>

void Launcher::companionSocialPrepare(companion::Look look) {
    using namespace companion;
    auto& standing = companionAffinity();
    auto& self = companionMindState().of(lookKey(look));
    const auto relation = standing.relationOf(lookKey(look));
    if (!self.social.initialized) {
        const auto* c = character(look);
        const auto points = affinity::pointsOf(standing.points, lookKey(look));
        const bool known = self.last_talk || !companionMemory().notesOf(lookKey(look)).empty() ||
            std::any_of(companionMemory().summaries.begin(), companionMemory().summaries.end(), [&](const auto& e) { return e.look == lookKey(look); });
        const auto previous = points > 0 ? affinity::effectiveStage(affinity::level(points), relation, c ? c->style : RelationStyle::balanced) :
            known ? affinity::unlevelled_stage : affinity::Stage::strangers;
        std::optional<RelationRole> agreement;
        if (relation.role != RelationRole::auto_role || previous == affinity::Stage::long_distance)
            agreement = affinity::effectiveRole(relation.role, c ? c->default_role : RelationRole::auto_role, c ? c->attraction : Attraction::any,
                                                 companionPerson().gender, relation.stance);
        self.social.seed(previous, agreement);
    }
    // Import existing promises/follow-ups once by stable id; the source records remain intact.
    const auto import = [&](const std::string& id, const std::wstring& text, uint64_t at, uint64_t due, const char* actor) {
        if (self.social.commitments.size() >= 128 || std::any_of(self.social.commitments.begin(), self.social.commitments.end(), [&](const auto& p) { return p.legacy_id == id; })) return;
        social::Commitment p;
        p.id = "p" + std::to_string(self.social.next_id++); p.legacy_id = id; p.actor = actor;
        p.text = text; p.made = p.updated = at; p.due = due; p.status = social::CommitmentStatus::agreed;
        self.social.commitments.push_back(std::move(p));
    };
    for (const auto& goal : self.goals)
        if (goal.kind == mind::Goal::Kind::promise && goal.status == mind::Goal::Status::open) import(goal.id, goal.text, goal.made, goal.due, "companion");
    for (const auto& follow : self.follow_ups)
        if (follow.state == 0) import(follow.id, follow.what, follow.made, follow.due, "player");
}

void Launcher::companionSocialAxes(companion::Look look, companion::social::Delta delta, const std::wstring& reason, const std::string& turn) {
    if (!delta.trust && !delta.respect && !delta.security) return;
    const auto key = std::string(companion::lookKey(look));
    auto& standing = companionAffinity();
    const auto before = standing.relationOf(key);
    if (!standing.adjustAxes(key, delta.trust, delta.respect, delta.security)) return;
    const auto after = standing.relationOf(key);
    const auto context = companionMindContext(look);
    companionMindState().of(key).social.record(turn, "axes", "both", reason, context.now,
        {after.trust - before.trust, after.respect - before.respect, after.security - before.security}, 4);
    saveCompanionAffinity();
}

void Launcher::companionSocialApply(const CompanionMessage& message, const companion::mind::Appraisal& appraisal) {
    const auto look = companion::lookFromKey(message.reply_look);
    if (!look || message.social_turn.empty()) return;
    companionSocialPrepare(*look);
    auto& self = companionMindState().of(message.reply_look);
    const auto context = companionMindContext(*look);
    companion::social::Context c;
    c.now = context.now; c.turn = message.social_turn; c.player_text = message.player_text;
    c.trust = context.trust; c.tension = context.tension; c.energy = companion::mind::energyNow(self, context);
    const auto p = message.social_plan.allowed.empty() ? companion::social::plan(self.social, c) : message.social_plan;
    const auto result = companion::social::apply(self.social, appraisal.social, p, c, message.reply.text,
        [&](std::wstring_view when) { return companion::social::deadline(when, context.now); });
    if (!result.changed) return;
    companionSocialAxes(*look, result.delta, appraisal.social.quote, c.turn);
    if (result.reward) {
        auto& policy = self.policy;
        const float error = *result.reward - policy.baseline_reward;
        policy.baseline_reward = std::clamp(policy.baseline_reward + 0.15f * error, 0.0f, 1.0f);
        const auto learn = [&](float& weight) { weight = std::clamp(weight + 0.16f * error, 0.15f, 0.95f); };
        if (policy.last_style == "teasing") learn(policy.teasing);
        else if (policy.last_style == "deep") learn(policy.deep_questions);
        else if (policy.last_style == "games") learn(policy.game_talk);
        else if (policy.last_style == "life") learn(policy.own_life);
        else if (policy.last_style == "warmth") learn(policy.warmth);
    }
    if (result.agreement) {
        companionAffinity().relation(message.reply_look).role = *result.agreement;
        saveCompanionAffinity();
    }
    for (const auto& promise : self.social.commitments) {
        for (auto& goal : self.goals) if (goal.id == promise.legacy_id) {
            goal.due = promise.due;
            if (promise.status == companion::social::CommitmentStatus::done) goal.status = companion::mind::Goal::Status::done;
            if (promise.status == companion::social::CommitmentStatus::cancelled || promise.status == companion::social::CommitmentStatus::declined)
                goal.status = companion::mind::Goal::Status::dropped;
        }
        for (auto& follow : self.follow_ups) if (follow.id == promise.legacy_id) {
            follow.due = promise.due;
            if (promise.status == companion::social::CommitmentStatus::done) follow.state = 3;
            if (promise.status == companion::social::CommitmentStatus::cancelled || promise.status == companion::social::CommitmentStatus::declined) follow.state = 2;
        }
    }
}
