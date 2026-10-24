// Launchfeed: only the selected companion publishes. Folder reads, LLM calls and durable
// commits run on workers; stale profile/folder replies are rejected by their generation.
#include "launcher.hpp"
#include "../platform/text_encoding.hpp"
#include <random>

namespace {
bool englishFeed() { return i18n::language() == i18n::Language::en; }
}

void Launcher::companionFeedReset() {
    auto& feed = companion_.feed;
    feed.cancel->store(true);
    const auto generation = feed.generation + 1;
    feed = {};
    feed.generation = generation;
    feed.look = std::string(companion::lookKey(companionLook()));
    feed.source = settings_.companion_feed_folder;
}

void Launcher::companionFeed(bool open) {
    auto& chat = companion_.chat;
    auto& view = companion_.feed.view;
    if (!settings_.companion || (open && chat.archive)) return;
    if (open && chat.feed && !view.exiting) return;
    chat.menu = chat.confirm = false;
    companion_.link_pointer.reset();
    closeKeyboard();
    if (open) {
        chat.viewer = chat.mind = false;
        chat.viewer_motion = chat.mind_motion = 0;
        companionHush();
        pollCompanionFeed(true);
        chat.feed = true;
        view.animate = settings_.screen_transitions;
        view.exiting = view.dm_exit = false;
        view.app_age = 0; view.focus = 0; view.thread.clear();
        view.entrance.snap(view.animate ? 0.0f : 1.0f);
        companionFeedImages();
        audio_.context();
    } else {
        if (!view.thread.empty()) companion_.feed.drafts[view.thread] = view.input;
        view.exiting = true;
        if (!settings_.screen_transitions) { chat.feed = false; view.entrance.snap(0); }
        chat.focus = view.dm_exit ? companion_ui::focus_input : companion_ui::focus_feed;
        audio_.navigate();
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::pollCompanionFeed(bool force) {
    auto& feed = companion_.feed;
    const auto look = companionLook();
    const auto key = std::string(companion::lookKey(look));
    if (feed.look != key || feed.source != settings_.companion_feed_folder) companionFeedReset();
    feed.view.configured = !settings_.companion_feed_folder.empty();
    feed.view.can_discuss = !companion_.chat.ended && !companion_.chat.archive;
    if (!settings_.companion || !companion_.persist || feed.view.busy ||
        (!feed.view.configured && !companion_.chat.feed && !force) || (!force && companion_.age < 8)) return;
    const auto tick = GetTickCount64();
    if (tick < feed.next_poll) return;
    feed.next_poll = tick + 5 * 60 * 1000; // Opening the phone repeatedly does not retry the model.
    feed.view.busy = true;
    const bool en = englishFeed();
    const auto local = companion_.clock();
    const int day = local.wYear * 10000 + local.wMonth * 100 + local.wDay;
    const auto now = companionNow();
    const bool awake = !companionAsleep(look);
    const bool publish = feed.view.configured && awake;
    // A light connection to her mind: her own goal and public, completed episodes only. No
    // private thoughts, player notes or forced mirroring of everything in the graph.
    std::wstring public_context;
    if (publish && settings_.companion_memory) {
        const auto& self = companionMindState().of(key);
        for (const auto& goal : self.goals)
            if (goal.kind == companion::mind::Goal::Kind::own && goal.status == companion::mind::Goal::Status::open) {
                public_context += goal.text + L". "; break;
            }
        int taken = 0;
        for (auto it = self.episodes.rbegin(); it != self.episodes.rend() && taken < 2; ++it)
            if (!it->hidden && it->state == companion::mind::Episode::State::happened) { public_context += it->what + L". "; ++taken; }
    }
    const auto system = companion::feed::generationPrompt(companionPersonaOf(look), look, en, public_context);
    const auto post = [hwnd = hwnd_, alive = alive_](std::unique_ptr<CompanionFeedMessage> message) {
        if (alive->load() && PostMessageW(hwnd, companion_feed_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    };
    const uint32_t seed = companion_.seed = companion_.seed * 1664525u + 1013904223u;
    const auto authors = companionFeedAuthors(seed);
    const auto author_name = std::wstring(companion::name(look));
    const auto reaction_system = companion::diaryPrompt(companionPersonaOf(look), look, en);
    companionSyncPerson(); // Her posts speak of him or her, as he or she is.
    work_.start([post, key, generation = feed.generation, cancel = feed.cancel, source = feed.source,
                 folder = companion::feed::folderOf(userConfigDirectory(), key), system, access = companionAccess(), transport = companion_.post,
                 alive = alive_, clock = companion_.clock, day, now, seed, publish, awake, en, authors, author_name, reaction_system] {
        auto message = std::make_unique<CompanionFeedMessage>();
        message->generation = generation; message->look = key;
        try {
            auto state = companion::feed::load(folder);
            if (!state) message->status = en ? L"Could not read the post archive. Publication paused." : L"Nie można odczytać archiwum postów. Publikowanie wstrzymane.";
            else {
                message->archive = *state;
                if (publish && companion::feed::due(*state, day, now) && !cancel->load() && alive->load()) {
                    if (const auto draft = companion::feed::pick(source, key, *state, seed)) {
                        const auto connection = access.resolve();
                        if (!connection.ready()) message->status = en ? L"Connect a model in Settings to publish." : L"Połącz model w ustawieniach, aby publikować.";
                        else {
                            auto progress = std::make_unique<CompanionFeedMessage>(*message);
                            progress->complete = false;
                            progress->status = en ? L"Writing a new post…" : L"Pisze nowy post…";
                            post(std::move(progress));
                            const auto reply = companion::ask(connection, system, {{true, companion::feed::generationRequest(draft->description, en)}}, transport, 300, 0.85f);
                            if (!cancel->load() && alive->load()) {
                                if (!reply.ok || reply.text.empty() || reply.text.size() > 480)
                                    message->status = en ? L"The model did not finish the post. The photo will wait." : L"Model nie ukończył posta. Zdjęcie poczeka.";
                                else {
                                    const auto time = clock(); // Count against the day of completion, including midnight.
                                    const int completed_day = time.wYear * 10000 + time.wMonth * 100 + time.wDay;
                                    const auto completed_at = companion::chats::localSeconds(time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
                                    if (auto saved = companion::feed::publish(folder, *draft, reply.text, completed_day, completed_at)) message->archive = std::move(*saved);
                                    else message->status = en ? L"Could not save the post, or its daily slot is no longer available." : L"Nie zapisano posta albo jego dzienny limit został już wykorzystany.";
                                }
                            }
                        }
                    } else if (state->posts.empty()) message->status = en ? L"Add a photo and its matching TXT description to this profile's folder." : L"Dodaj zdjęcie i pasujący opis TXT do folderu tej postaci.";
                }
                // Seed one new/older post per pass; stored comments never reshuffle on opening.
                if (message->archive && !cancel->load() && alive->load()) {
                    for (auto& item : message->archive->posts) {
                        if (item.social.seeded) continue;
                        const auto connection = access.resolve();
                        if (!connection.ready()) break;
                        const auto reply = companion::ask(connection, companion::feed::commentsPrompt(item, authors, en), {{true, L"JSON"}}, transport, 500, 0.9f);
                        if (cancel->load() || !alive->load()) break;
                        const auto comments = companion::feed::parseComments(reply.raw, authors, std::max(now, item.at));
                        if (reply.ok && !comments.empty())
                            if (auto saved = companion::feed::seedComments(folder, item.id, comments)) item.social = std::move(*saved);
                        break;
                    }
                    for (auto& item : message->archive->posts) {
                        if (!awake) break;
                        if (!item.social.reaction_pending || cancel->load() || !alive->load()) continue;
                        const auto connection = access.resolve();
                        if (!connection.ready()) break;
                        const auto reply = companion::ask(connection, reaction_system, {{true, companion::feed::reactionPrompt(item, en)}}, transport, 220, 0.85f);
                        if (cancel->load() || !alive->load()) break;
                        if (reply.ok)
                            if (auto saved = companion::feed::completeReaction(folder, item.id, item.social.reaction_context, reply.text, author_name, key, now)) item.social = std::move(*saved);
                        break;
                    }
                }
            }
        } catch (...) { message->status = en ? L"Launchfeed could not load or publish a post." : L"Launchfeed nie mógł wczytać lub opublikować posta."; }
        post(std::move(message));
    });
}

void Launcher::onCompanionFeed(std::unique_ptr<CompanionFeedMessage> message) {
    auto& feed = companion_.feed;
    if (message->generation != feed.generation || message->look != companion::lookKey(companionLook()) ||
        feed.source != settings_.companion_feed_folder) return;
    if (message->kind == CompanionFeedMessage::Kind::image) {
        // Keep only the visible neighbourhood, even if an old read finished after scrolling.
        if (feed.requested_images.contains(message->image_id))
            feed.view.images[message->image_id] = std::make_shared<const std::vector<uint8_t>>(std::move(message->bytes));
        for (auto& p : feed.view.archive.posts) if (p.id == message->image_id && message->width && message->height) { p.width = message->width; p.height = message->height; }
    } else if (message->kind == CompanionFeedMessage::Kind::social || message->kind == CompanionFeedMessage::Kind::private_message) {
        if (message->kind == CompanionFeedMessage::Kind::private_message) feed.claiming = false;
        else feed.view.social_busy = false;
        if (message->social) {
            for (auto& p : feed.view.archive.posts) if (p.id == message->image_id && p.social.revision <= message->social->revision) p.social = *message->social;
            if (!message->submitted.empty() && feed.view.thread == message->image_id && feed.view.input == message->submitted) {
                feed.view.input.clear();
                using L = companion_ui::FeedLayout;
                for (size_t i = 0; i < feed.view.archive.posts.size(); ++i) if (feed.view.archive.posts[i].id == message->image_id) {
                    feed.view.scroll = std::clamp(L::position(feed.view.archive, i, feed.view.thread) + L::cardHeight(feed.view.archive.posts[i], feed.view.thread) -
                        (L::content(feed.view.thread).bottom - L::top), 0.0f, L::limit(feed.view.archive, feed.view.thread));
                    if (!settings_.screen_transitions) feed.view.motion.snap(feed.view.scroll);
                }
            }
            if (!message->submitted.empty() && feed.drafts[message->image_id] == message->submitted) feed.drafts[message->image_id].clear();
            if (message->kind == CompanionFeedMessage::Kind::private_message) {
                feed.private_pending = message->social->private_text;
                for (const auto& p : feed.view.archive.posts) if (p.id == message->image_id) feed.private_context = companion::feed::reference(p, englishFeed());
            }
            else { feed.view.status.clear(); feed.next_poll = 0; }
        } else if (message->kind == CompanionFeedMessage::Kind::social) feed.view.status = tr(L"Nie zapisano reakcji. Spróbuj ponownie.");
    } else {
        feed.view.busy = !message->complete;
        feed.view.loaded = true;
        feed.view.status = std::move(message->status);
        if (message->archive) {
            const auto anchor = !feed.view.archive.posts.empty() ? feed.view.archive.posts.front().id : std::string{};
            auto& posts = message->archive->posts;
            // A scan that started before a click cannot undo the click when it finishes later.
            for (auto& p : posts) for (const auto& old : feed.view.archive.posts) if (p.id == old.id) {
                if (old.social.revision > p.social.revision) p.social = old.social;
                if (!p.width) { p.width = old.width; p.height = old.height; }
            }
            if (feed.view.scroll > 1 && !anchor.empty()) {
                const auto old = std::find_if(posts.begin(), posts.end(), [&](const auto& p) { return p.id == anchor; });
                if (old != posts.end()) {
                    const float offset = companion_ui::FeedLayout::position(*message->archive, static_cast<size_t>(old - posts.begin()), feed.view.thread);
                    feed.view.scroll += offset; feed.view.motion.value += offset;
                }
            }
            feed.view.archive = std::move(*message->archive);
        }
    }
    companionFeedImages();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionFeedImages() {
    if (!companion_.chat.feed) return;
    auto& feed = companion_.feed;
    const auto& posts = feed.view.archive.posts;
    const auto [first, last] = companion_ui::FeedLayout::visible(feed.view.archive, feed.view.motion.value, feed.view.thread);
    std::set<std::string> wanted;
    for (int i = std::max(0, first - 1); i < std::min(static_cast<int>(posts.size()), last + 1); ++i) wanted.insert(posts[i].id);
    std::erase_if(feed.view.images, [&](const auto& item) { return !wanted.contains(item.first); });
    std::erase_if(feed.requested_images, [&](const auto& id) { return !wanted.contains(id); });
    for (const auto& id : wanted) {
        if (!feed.requested_images.insert(id).second) continue;
        work_.start([hwnd = hwnd_, alive = alive_, key = feed.look, generation = feed.generation, id,
                     folder = companion::feed::folderOf(userConfigDirectory(), feed.look)] {
            auto message = std::make_unique<CompanionFeedMessage>();
            message->kind = CompanionFeedMessage::Kind::image;
            message->look = key; message->generation = generation; message->image_id = id;
            if (auto bytes = companion::feed::image(folder, id)) {
                const auto [w, h] = companion::feed::dimensions(*bytes);
                message->width = w; message->height = h;
                message->bytes = std::move(*bytes);
            }
            if (alive->load() && PostMessageW(hwnd, companion_feed_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
        });
    }
}

void Launcher::companionFeedScroll(float amount) {
    auto& view = companion_.feed.view;
    if (!view.ready()) return;
    view.scroll = std::clamp(view.scroll + amount, 0.0f, companion_ui::FeedLayout::limit(view.archive, view.thread));
    view.hover = companion_ui::FeedLayout::no_hit;
    if (!settings_.screen_transitions) view.motion.snap(view.scroll);
    companionFeedImages();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionFeedDiscuss(int index) {
    auto& feed = companion_.feed;
    if (index < 0 || index >= static_cast<int>(feed.view.archive.posts.size()) || companion_.chat.ended || companion_.chat.archive) { navigationBoundary(); return; }
    feed.reference = companion::feed::reference(feed.view.archive.posts[index], englishFeed());
    feed.reference_quote = L"Launchfeed · " + companion::chats::label(feed.view.archive.posts[index].at, englishFeed()) + L"\n„" + feed.view.archive.posts[index].text + L"”";
    feed.view.dm_exit = true;
    companionFeed(false);
    auto& chat = companion_.chat;
    chat.focus = companion_ui::focus_input;
    if (chat.input.empty()) chat.input = englishFeed() ? L"About your post… " : L"W sprawie twojego posta… ";
    InvalidateRect(hwnd_, nullptr, FALSE);
}

std::vector<companion::feed::CommentAuthor> Launcher::companionFeedAuthors(uint32_t seed) {
    using Author = companion::feed::CommentAuthor;
    std::vector<Author> visitors{{L"mila.jpg", L"warm, fashion and photography", {}}, {L"neonfox", L"brief, playful, never intrusive", {}},
        {L"kawa_i_kadry", L"cozy everyday moments", {}}, {L"pixelmood", L"curious, notices details actually in the photo", {}},
        {L"ola.afterhours", L"casual and supportive", {}}, {L"softfocus", L"photography enthusiast", {}}};
    std::mt19937 random(seed);
    std::shuffle(visitors.begin(), visitors.end(), random);
    visitors.resize(3 + seed % 2);
    // A companion may join when the authored biographies know each other and their current
    // relationship isn't tense. No guest receives private player memories to write a comment.
    const auto look = companionLook();
    const auto* owner = companion::character(look);
    if (owner && seed % 100 < 45) {
        std::vector<companion::Look> friends;
        for (const auto other : companion::availableLooks()) {
            const auto* c = companion::character(other);
            if (other == look || !c) continue;
            const bool mutual = owner->about(false, false).find(c->name) != std::wstring::npos && c->about(false, false).find(owner->name) != std::wstring::npos;
            const int tension = companion_.cognition ? companion_.cognition->peerTension(owner->key, c->key) : 0;
            if (mutual && tension < 25) friends.push_back(other);
        }
        if (!friends.empty()) {
            const auto friend_look = friends[(seed >> 8) % friends.size()];
            visitors.back() = {companion::name(friend_look), companion::personaName(companionPersonaOf(friend_look)), std::string(companion::lookKey(friend_look))};
        }
    }
    return visitors;
}

void Launcher::companionFeedAction(int hit) {
    auto& feed = companion_.feed;
    auto& view = feed.view;
    using L = companion_ui::FeedLayout;
    if (hit == L::close_hit) { companionFeed(false); return; }
    if (!view.ready()) return;
    if (hit == L::thread_back) { companionFeedBack(); return; }
    if (hit == L::input_hit) { view.focus = 3; if (pad_input_) openKeyboard(); InvalidateRect(hwnd_, nullptr, FALSE); return; }
    if (hit == L::send_hit) { companionFeedComment(); return; }
    if (hit < 0) return;
    const int index = hit / 3, action = hit % 3;
    if (index >= static_cast<int>(view.archive.posts.size())) return;
    const auto& post = view.archive.posts[index];
    view.focus = action;
    if (action == 2) { companionFeedDiscuss(index); return; }
    if (action == 1) {
        if (view.thread == post.id) { companionFeedBack(); return; }
        if (!view.thread.empty()) feed.drafts[view.thread] = view.input;
        view.thread = post.id; view.input = feed.drafts[post.id]; view.focus = 3;
        view.scroll = std::min(L::limit(view.archive, view.thread), L::position(view.archive, index, view.thread) + L::actionsTop(post));
        if (!settings_.screen_transitions) view.motion.snap(view.scroll);
        closeKeyboard();
        if (pad_input_) openKeyboard();
        audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE); return;
    }
    if (view.social_busy || !companion_.persist) { navigationBoundary(); return; }
    view.social_busy = true;
    if (!post.social.liked) { view.heart = post.id; view.heart_age = 0; }
    const auto seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    work_.start([hwnd = hwnd_, alive = alive_, cancel = feed.cancel, key = feed.look, generation = feed.generation, id = post.id,
                 liked = !post.social.liked, seed, folder = companion::feed::folderOf(userConfigDirectory(), feed.look)] {
        auto message = std::make_unique<CompanionFeedMessage>();
        message->kind = CompanionFeedMessage::Kind::social;
        message->look = key; message->generation = generation; message->image_id = id;
        if (!cancel->load() && alive->load()) message->social = companion::feed::setLike(folder, id, liked, seed);
        if (alive->load() && PostMessageW(hwnd, companion_feed_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionFeedComment() {
    auto& feed = companion_.feed;
    auto& view = feed.view;
    if (!view.ready() || view.thread.empty() || view.social_busy || !companion_.persist || view.input.find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        if (pad_input_ && view.typing() && view.input.empty()) openKeyboard();
        else navigationBoundary();
        return;
    }
    view.social_busy = true;
    closeKeyboard();
    const auto seed = companion_.seed = companion_.seed * 1664525u + 1013904223u + static_cast<uint32_t>(GetTickCount64());
    work_.start([hwnd = hwnd_, alive = alive_, cancel = feed.cancel, key = feed.look, generation = feed.generation, id = view.thread,
                 text = view.input, now = companionNow(), seed, folder = companion::feed::folderOf(userConfigDirectory(), feed.look)] {
        auto message = std::make_unique<CompanionFeedMessage>();
        message->kind = CompanionFeedMessage::Kind::social;
        message->look = key; message->generation = generation; message->image_id = id; message->submitted = text;
        if (!cancel->load() && alive->load()) message->social = companion::feed::addComment(folder, id, text, now, seed);
        if (alive->load() && PostMessageW(hwnd, companion_feed_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
    });
    audio_.context(); InvalidateRect(hwnd_, nullptr, FALSE);
}

void Launcher::companionFeedBack() {
    auto& feed = companion_.feed;
    if (!feed.view.thread.empty() && !feed.view.exiting) {
        feed.drafts[feed.view.thread] = feed.view.input;
        feed.view.thread.clear(); feed.view.input.clear(); feed.view.focus = 1;
        closeKeyboard(); companionFeedScroll(0); return;
    }
    companionFeed(false);
}

void Launcher::companionFeedMove(int dx, int dy) {
    auto& view = companion_.feed.view;
    if (!view.ready()) return;
    if (dy) companionFeedScroll(dy * 150.0f);
    else if (dx) {
        view.focus = view.thread.empty() ? (view.focus + dx + 3) % 3 : (view.focus == 3 ? 4 : 3);
        audio_.navigate(); InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

bool Launcher::tickCompanionFeed(float dt) {
    auto& feed = companion_.feed;
    auto& view = feed.view;
    auto& chat = companion_.chat;
    bool moving = false;
    if (chat.feed) {
        view.app_age += dt;
        const float target = view.exiting ? 0.0f : 1.0f;
        if (view.animate) view.entrance.step(target, dt, view.exiting ? 17.0f : 14.0f, view.exiting ? 1.0f : 0.76f);
        else view.entrance.snap(target);
        if (view.entrance.settled(target, 0.001f)) {
            view.entrance.snap(target);
            if (view.exiting) { chat.feed = false; chat.focus = view.dm_exit ? companion_ui::focus_input : companion_ui::focus_feed; }
        } else moving = true;
        if (view.app_age < 2) moving = true;
        if (view.heart_age < 0.6f) { view.heart_age += dt; moving = true; }
        view.scroll = std::clamp(view.scroll, 0.0f, companion_ui::FeedLayout::limit(view.archive, view.thread));
        if (settings_.screen_transitions) view.motion.step(view.scroll, dt, spring::glide.omega, spring::glide.zeta);
        else view.motion.snap(view.scroll);
        if (!view.motion.settled(view.scroll, 0.2f)) moving = true; else view.motion.snap(view.scroll);
        companionFeedImages();
    }
    const bool idle = companionOpen() && !chat.feed && !chat.mind && !chat.viewer && !chat.menu && !chat.confirm && !chat.archive && !chat.ended &&
        !chat.waiting && !chat.talking() && !companion_.busy && chat.input.empty() && !companion_.guest.look && !keyboard_.open && !companionAsleep(companionLook());
    if (idle && !feed.private_pending.empty()) {
        const auto reply = companion::parseText(std::exchange(feed.private_pending, std::wstring{}));
        companion_.turns.push_back({true, std::exchange(feed.private_context, std::wstring{})});
        companion_.turns.push_back({false, reply.raw});
        companion_ui::Message message{false, reply.text, reply.emotions}; message.speaker = companionLook(); message.free = true;
        chat.log.push_back(std::move(message)); chat.reveal = 0; chat.reveal_clock = 0; chat.scroll = 0;
        companion_.rig.emote(reply.emotions.empty() ? companion::Emotion::smile : reply.emotions.front().emotion);
        companionSaveChat(); moving = true;
    } else if (idle && !feed.claiming && companion_.persist && GetTickCount64() >= feed.next_private) {
        for (const auto& post : view.archive.posts) {
            if (post.social.private_text.empty() || post.social.private_claimed || !post.social.liked) continue;
            feed.claiming = true;
            feed.next_private = GetTickCount64() + 30'000;
            work_.start([hwnd = hwnd_, alive = alive_, cancel = feed.cancel, key = feed.look, generation = feed.generation,
                         id = post.id, folder = companion::feed::folderOf(userConfigDirectory(), feed.look)] {
                auto message = std::make_unique<CompanionFeedMessage>();
                message->kind = CompanionFeedMessage::Kind::private_message;
                message->look = key; message->generation = generation; message->image_id = id;
                if (!cancel->load() && alive->load()) message->social = companion::feed::claimPrivate(folder, id);
                if (alive->load() && PostMessageW(hwnd, companion_feed_message, 0, reinterpret_cast<LPARAM>(message.get()))) message.release();
            });
            break;
        }
    }
    return moving;
}
