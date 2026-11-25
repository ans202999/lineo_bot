#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <tgbot/tgbot.h>
#include <nlohmann/json.hpp>

// #include "consts.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;

enum Roles {
    Admin,
    Sales,
    Project,
    Production,
    Purchase,
    Assemble,
    Marketing,
    Unknown
};

std::string roleToString(Roles role) {
    switch (role) {
        case Admin:      return "Admin";
        case Sales:      return "Sales";
        case Project:    return "Project";
        case Production: return "Production";
        case Purchase:   return "Purchase";
        case Assemble:   return "Assemble";
        case Marketing:  return "Marketing";
        case Unknown:
        default:         return "Unknown";
    }
}

Roles roleFromString(const std::string& raw) {
    std::string lower = raw;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (lower == "admin")      return Admin;
    if (lower == "sales")      return Sales;
    if (lower == "project")    return Project;
    if (lower == "production") return Production;
    if (lower == "purchase")   return Purchase;
    if (lower == "assemble")   return Assemble;
    if (lower == "marketing")  return Marketing;
    return Unknown;
}

struct StatusTransition {
    std::string expectedStatus;
    std::string nextStatus;
    std::string callbackPrefix;
};

bool getTransitionForRole(Roles role, const std::string& currentStatus, StatusTransition& out) {
    if (role == Project && currentStatus == "Project") {
        out = {"Project", "закуп", "order_done:"};
        return true;
    }
    if (role == Purchase && currentStatus == "закуп") {
        out = {"закуп", "производство", "order_done:"};
        return true;
    }
    return false;
}

struct StageRule {
    Roles role;
    std::string currentStatus;
    std::string nextStatus;
    bool hasDoneButton;
};

// Порядок этапов по ролям
inline const std::vector<StageRule>& getStageRules() {
    static const std::vector<StageRule> rules = {
        {Sales,      "создан",         "проектирование", true},
        {Project,    "проектирование", "закуп",          true},
        {Purchase,   "закуп",          "производство",   true},
        {Production, "производство",   "сборка",         true},
        {Assemble,   "сборка",         "монтаж",         true},
        {Assemble,   "монтаж",         "заказ выполнен", true}
    };
    return rules;
}

inline std::optional<StageRule> findRuleByRole(Roles role) {
    for (const auto& r : getStageRules()) {
        if (r.role == role) {
            return r;
        }
    }
    return std::nullopt;
}

std::optional<std::string> findNextStatus(const std::string& currentStatus) {
    for (const auto& r : getStageRules()) {
        if (r.currentStatus == currentStatus) {
            return r.nextStatus;
        }
    }
    return std::nullopt;
}

constexpr const char* kUsersFilePath = "data/users.json";
// Замените на ваш реальный Telegram user_id для доступа к /users
// constexpr std::int64_t kAdminUserId = 123;
// constexpr std::int64_t kAdminUserId = 102719261;
constexpr std::int64_t kAdminUserId = 883160581;

struct BotUser {
    std::int64_t id = 0;
    std::string name;
    std::string username;
    Roles role = Unknown;
    std::int64_t lastSeen = 0;
};

std::string buildUserName(const TgBot::User::Ptr& from) {
    if (!from) {
        return "Unknown";
    }
    std::string fullName = from->firstName;
    if (from->lastName.has_value() && !from->lastName->empty()) {
        if (!fullName.empty()) {
            fullName += " ";
        }
        fullName += *from->lastName;
    }
    return fullName.empty() ? "Unknown" : fullName;
}

void to_json(json& j, const BotUser& u) {
    j = json{
        {"id", u.id},
        {"name", u.name},
        {"username", u.username},
        {"role", roleToString(u.role)},
        {"last_seen", u.lastSeen}
    };
}

void from_json(const json& j, BotUser& u) {
    u.id       = j.value("id", std::int64_t{0});
    u.name     = j.value("name", "Unknown");
    u.username = j.value("username", "");
    u.role     = roleFromString(j.value("role", std::string("Unknown")));
    u.lastSeen = j.value("last_seen", std::int64_t{0});
}

Roles getUserRole(const std::vector<BotUser>& users, std::int64_t userId) {
    if (userId == kAdminUserId) {
        return Admin;
    }
    auto it = std::find_if(users.begin(), users.end(), [userId](const BotUser& u) {
        return u.id == userId;
    });
    if (it == users.end()) {
        return Unknown;
    }
    return it->role;
}

bool loadUsersFromFile(const std::string& path, std::vector<BotUser>& outUsers) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    try {
        json j;
        ifs >> j;
        if (!j.is_array()) {
            return false;
        }
        outUsers = j.get<std::vector<BotUser>>();
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "Failed to parse users file (" << path << "): " << ex.what() << "\n";
        return false;
    }
}

bool saveUsers(const fs::path& path, const std::vector<BotUser>& users) {
    try {
        if (path.has_parent_path()) {
            fs::create_directories(path.parent_path());
        }

        const fs::path tmpPath = path.string() + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "[ERROR] Не удалось открыть временный файл: " << tmpPath << "\n";
                return false;
            }
            const json j = users;
            out << j.dump(2) << "\n";
        }
        fs::rename(tmpPath, path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Не удалось сохранить пользователей: " << e.what() << "\n";
        return false;
    }
}

// Добавляет нового пользователя или обновляет имя/username, если он уже есть
bool trackUser(std::vector<BotUser>& users, const TgBot::User::Ptr& from) {
    if (!from) {
        return false;
    }

    const std::int64_t userId = from->id;
    const std::string name = buildUserName(from);
    const std::string username = from->username.value_or("");
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));

    auto it = std::find_if(users.begin(), users.end(), [userId](const BotUser& u) {
        return u.id == userId;
    });

    if (it == users.end()) {
        const Roles initialRole = (userId == kAdminUserId) ? Admin : Unknown;
        users.push_back(BotUser{userId, name, username, initialRole, now});
        return true;
    }

    const bool changed = (it->name != name) || (it->username != username);
    it->name = name;
    it->username = username;
    it->lastSeen = now;
    return changed;
}

enum class OrderStep {
    None,
    ObjectName,
    ClientName,
    ClientPhone,
    Notes
};

struct OrderDraft {
    OrderStep step = OrderStep::None;
    std::string objectName;
    std::string clientName;
    std::string clientPhone;
    std::string notes;
};

struct OrderNote {
    std::string role;
    std::string text;
};

void to_json(json& j, const OrderNote& n) {
    j = json{
        {"role", n.role},
        {"text", n.text}
    };
}

void from_json(const json& j, OrderNote& n) {
    n.role = j.value("role", "");
    n.text = j.value("text", "");
}

struct NoteDraft {
    bool waitingNote = false;
    int orderId = 0;
};

struct OrderCard {
    int id = 0;
    std::int64_t managerId = 0;
    std::string objectName;
    std::string clientName;
    std::string clientPhone;
    std::vector<OrderNote> notes;
    std::string status = "создан";
    std::string assigneeName;
    bool isBlocked = false;
    std::string blockReason;
};

void to_json(json& j, const OrderCard& o) {
    j = json{
        {"id", o.id},
        {"manager_id", o.managerId},
        {"object_name", o.objectName},
        {"client_name", o.clientName},
        {"client_phone", o.clientPhone},
        {"notes", o.notes},
        {"status", o.status},
        {"assignee_name", o.assigneeName},
        {"is_blocked", o.isBlocked},
        {"block_reason", o.blockReason}
    };
}

void from_json(const json& j, OrderCard& o) {
    o.id           = j.value("id", 0);
    o.managerId    = j.value("manager_id", std::int64_t{0});
    o.objectName   = j.value("object_name", "");
    o.clientName   = j.value("client_name", "");
    o.clientPhone  = j.value("client_phone", "");
    o.status       = j.value("status", std::string("создан"));
    o.assigneeName = j.value("assignee_name", "");
    o.isBlocked    = j.value("is_blocked", false);
    o.blockReason  = j.value("block_reason", "");

    o.notes.clear();
    if (j.contains("notes")) {
        if (j["notes"].is_array()) {
            o.notes = j["notes"].get<std::vector<OrderNote>>();
        } else if (j["notes"].is_string()) {
            const std::string legacyNote = j["notes"].get<std::string>();
            if (!legacyNote.empty()) {
                o.notes.push_back(OrderNote{"Sales", legacyNote});
            }
        }
    }
}

struct BlockDraft {
    bool waitingReason = false;
    int orderId = 0;
};

std::unordered_map<std::int64_t, OrderDraft> drafts;
std::unordered_map<std::int64_t, BlockDraft> blockDrafts;

bool canRoleAccessOrder(Roles role, const OrderCard& order) {
    if (role == Admin) {
        return true;
    }
    if (role == Assemble) {
        return order.status == "сборка" || order.status == "монтаж";
    }
    if (auto ruleOpt = findRuleByRole(role); ruleOpt.has_value()) {
        return order.status == ruleOpt->currentStatus;
    }
    return false;
}

constexpr const char* kBtnListOrders = "Список заказов";
constexpr const char* kBtnCreateOrder = "Создать заказ";
constexpr const char* kBtnCancelOrder = "Отмена";

constexpr const char* kOrdersFilePath = "data/orders.json";

TgBot::ReplyKeyboardMarkup::Ptr makeSalesKeyboard() {
    auto kb = std::make_shared<TgBot::ReplyKeyboardMarkup>();
    kb->resizeKeyboard = true;

    auto btnCreate = std::make_shared<TgBot::KeyboardButton>();
    btnCreate->text = kBtnCreateOrder;

    auto btnList = std::make_shared<TgBot::KeyboardButton>();
    btnList->text = kBtnListOrders;

    kb->keyboard.push_back({btnCreate, btnList});
    return kb;
}

TgBot::ReplyKeyboardMarkup::Ptr makeAdminKeyboard() {
    auto kb = std::make_shared<TgBot::ReplyKeyboardMarkup>();
    kb->resizeKeyboard = true;

    auto btnCreate = std::make_shared<TgBot::KeyboardButton>();
    btnCreate->text = kBtnCreateOrder;

    auto btnList = std::make_shared<TgBot::KeyboardButton>();
    btnList->text = kBtnListOrders;

    kb->keyboard.push_back({btnCreate, btnList});
    return kb;
}

TgBot::ReplyKeyboardMarkup::Ptr makeSingleButtonKeyboard(const std::string& buttonText) {
    auto kb = std::make_shared<TgBot::ReplyKeyboardMarkup>();
    kb->resizeKeyboard = true;

    auto btn = std::make_shared<TgBot::KeyboardButton>();
    btn->text = buttonText;

    kb->keyboard.push_back({btn});
    return kb;
}

TgBot::ReplyKeyboardMarkup::Ptr makeKeyboardForRole(Roles role) {
    if (role == Sales) {
        return makeSalesKeyboard();
    }
    if (role == Admin) {
        return makeAdminKeyboard();
    }
    if (findRuleByRole(role).has_value()) {
        return makeSingleButtonKeyboard(kBtnListOrders);
    }
    return nullptr;
}

TgBot::InlineKeyboardMarkup::Ptr makeDoneInlineKeyboard(int orderId) {
    auto kb = std::make_shared<TgBot::InlineKeyboardMarkup>();

    auto btnDone = std::make_shared<TgBot::InlineKeyboardButton>();
    btnDone->text = "Готово";
    btnDone->callbackData = "order_done:" + std::to_string(orderId);

    kb->inlineKeyboard.push_back({btnDone});
    return kb;
}

TgBot::ReplyKeyboardMarkup::Ptr makeCancelOrderKeyboard() {
    auto kb = std::make_shared<TgBot::ReplyKeyboardMarkup>();
    kb->resizeKeyboard = true;

    auto btnCancel = std::make_shared<TgBot::KeyboardButton>();
    btnCancel->text = kBtnCancelOrder;

    kb->keyboard.push_back({btnCancel});
    return kb;
}

TgBot::InlineKeyboardMarkup::Ptr makeOrderInlineKeyboard(const OrderCard& o, Roles role) {
    auto kb = std::make_shared<TgBot::InlineKeyboardMarkup>();
    std::vector<TgBot::InlineKeyboardButton::Ptr> row;

    const bool hasAccess = canRoleAccessOrder(role, o);

    if (hasAccess) {
        if (o.isBlocked) {
            auto btnUnblock = std::make_shared<TgBot::InlineKeyboardButton>();
            btnUnblock->text = "Разблокировать";
            btnUnblock->callbackData = "order_unblock:" + std::to_string(o.id);
            row.push_back(btnUnblock);
        } else {
            const bool canAdvance = findNextStatus(o.status).has_value();
            if (canAdvance) {
                auto btnDone = std::make_shared<TgBot::InlineKeyboardButton>();
                btnDone->text = "Готово";
                btnDone->callbackData = "order_done:" + std::to_string(o.id);
                row.push_back(btnDone);
            }

            auto btnBlock = std::make_shared<TgBot::InlineKeyboardButton>();
            btnBlock->text = "Заблокировано";
            btnBlock->callbackData = "order_block:" + std::to_string(o.id);
            row.push_back(btnBlock);
        }
    }

    if (hasAccess) {
        auto btnNote = std::make_shared<TgBot::InlineKeyboardButton>();
        btnNote->text = "Добавить заметку";
        btnNote->callbackData = "order_note:" + std::to_string(o.id);
        row.push_back(btnNote);
    }

    if (!row.empty()) {
        kb->inlineKeyboard.push_back(row);
    }
    return kb;
}
constexpr const char* kBtnProjectOrders = "Список заказов";

void updateOrders(const fs::path& path, std::vector<OrderCard>& orders) {
    if (!fs::exists(path)) {
        return;
    }

    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "[WARN] Не удалось открыть файл заказов: " << path << "\n";
        return;
    }

    try {
        json j;
        in >> j;
        if (j.is_array()) {
            orders = j.get<std::vector<OrderCard>>();
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Ошибка парсинга JSON (" << path << "): " << e.what() << "\n";
    }
}

bool saveOrders(const fs::path& path, const std::vector<OrderCard>& orders) {
    try {
        if (path.has_parent_path()) {
            fs::create_directories(path.parent_path());
        }

        // Записываем во временный файл и делаем atomic rename, чтобы не повредить базу при сбое
        const fs::path tmpPath = path.string() + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::trunc);
            if (!out.is_open()) {
                std::cerr << "[ERROR] Не удалось открыть временный файл: " << tmpPath << "\n";
                return false;
            }
            const json j = orders;
            out << j.dump(2) << "\n";
        }
        fs::rename(tmpPath, path);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Не удалось сохранить заказы: " << e.what() << "\n";
        return false;
    }
}

std::string formatSingleOrder(const OrderCard& o) {
    std::string res =
        "Заказ #" + std::to_string(o.id) + "\n"
        "-------------------------\n"
        "Статус: " + o.status + "\n"
        "Исполнитель: " + (o.assigneeName.empty() ? "-" : o.assigneeName) + "\n"
        "Заблокирован: " + (o.isBlocked ? "да" : "нет");

    if (o.isBlocked && !o.blockReason.empty()) {
        res += "\nПричина блокировки: " + o.blockReason;
    }

    res += "\nМенеджер ID: " + std::to_string(o.managerId) + "\n"
           "Имя объекта: " + o.objectName + "\n"
           "ФИО клиента: " + o.clientName + "\n"
           "Номер клиента: " + o.clientPhone + "\n"
           "Заметки:";

    if (o.notes.empty()) {
        res += " -";
    } else {
        for (const auto& n : o.notes) {
            res += "\n(" + n.role + ": " + n.text + ")";
        }
    }

    return res;
}

std::string formatOrderCard(std::int64_t userId, const OrderDraft& draft) {
    return "Карточка заказа создана\n"
           "-------------------------\n"
           "Менеджер ID: " + std::to_string(userId) + "\n"
           "Имя объекта: " + draft.objectName + "\n"
           "ФИО клиента: " + draft.clientName + "\n"
           "Номер клиента: " + draft.clientPhone + "\n"
           "Заметки: " + (draft.notes.empty() ? "-" : draft.notes);
}

std::string findUserNameById(const std::vector<BotUser>& users, std::int64_t userId) {
    auto it = std::find_if(users.begin(), users.end(), [userId](const BotUser& u) {
        return u.id == userId;
    });
    if (it != users.end() && !it->name.empty()) {
        return it->name;
    }
    return "ID " + std::to_string(userId);
}

int main() {
    const char* tokenEnv = std::getenv("BOT_TOKEN");
    if (!tokenEnv || std::string(tokenEnv).empty()) {
        std::cerr << "Ошибка: задайте переменную окружения BOT_TOKEN\n";
        return 1;
    }

    const std::string token(tokenEnv);
    TgBot::Bot bot(token);

    std::vector<BotUser> users;
    loadUsersFromFile(kUsersFilePath, users);
    std::cout << "Загружено пользователей из базы: " << users.size() << "\n";
    
    std::vector<OrderCard> orders;
    updateOrders(kOrdersFilePath, orders);
    int nextOrderId = 1;
    for (const auto& o : orders) {
        if (o.id >= nextOrderId) {
            nextOrderId = o.id + 1;
        }
    }
    std::cout << "Загружено заказов из базы: " << orders.size() << "\n";

    std::unordered_map<std::int64_t, OrderDraft> drafts;

    std::unordered_map<std::int64_t, NoteDraft> noteDrafts;

    bot.getEvents().onCommand("start", [&bot, &drafts, &users, &noteDrafts](TgBot::Message::Ptr message) {
        if (!message || !message->chat || !message->from) {
            return;
        }

        const std::int64_t chatId = message->chat->id;
        const std::int64_t userId = message->from->id;
        const auto userName = message->from->firstName;

        std::cout << "[/start] User: " << userName
                << " | user_id: " << userId
                << " | chat_id: " << chatId << "\n";


        loadUsersFromFile(kUsersFilePath, users);
        noteDrafts.erase(userId);
        
        const auto role = getUserRole(users, userId);
        std::cout << "Role is " << roleToString(role) << std::endl;
        if (auto kb = makeKeyboardForRole(role)) {
            bot.getApi().sendMessage(
                chatId,
                "Роль: " + roleToString(role) + ". Выберите действие:",
                nullptr,
                nullptr,
                kb
            );
            return;
        }
    });

    bot.getEvents().onAnyMessage([&bot, &drafts, &nextOrderId, &orders, &users, &noteDrafts](TgBot::Message::Ptr message) {

        if (!message || !message->chat || !message->from) {
            return;
        }

        // Регистрация всех, кто взаимодействует с ботом:
        const bool isNewOrUpdated = trackUser(users, message->from);
        if (isNewOrUpdated) {
            saveUsers(kUsersFilePath, users);
        }

        // Команды вроде /start обрабатываются в onCommand
        if (message->text.value_or("").starts_with("/")) {
            return;
        }

        const std::int64_t chatId = message->chat->id;
        const std::int64_t userId = message->from->id;
        const std::string text = message->text.value_or("");

        loadUsersFromFile(kUsersFilePath, users);
        updateOrders(kOrdersFilePath, orders);

        const auto role = getUserRole(users, userId);

        if (auto nIt = noteDrafts.find(userId); nIt != noteDrafts.end() && nIt->second.waitingNote) {
            if (text == kBtnCancelOrder) {
                noteDrafts.erase(userId);
                bot.getApi().sendMessage(
                    chatId,
                    "Добавление заметки отменено.",
                    nullptr,
                    nullptr,
                    makeKeyboardForRole(role)
                );
                return;
            }

            const int targetId = nIt->second.orderId;
            noteDrafts.erase(userId);

            auto oIt = std::find_if(orders.begin(), orders.end(), [targetId](const OrderCard& o) {
                return o.id == targetId;
            });

            if (oIt == orders.end()) {
                bot.getApi().sendMessage(
                    chatId,
                    "Заказ #" + std::to_string(targetId) + " не найден.",
                    nullptr,
                    nullptr,
                    makeKeyboardForRole(role)
                );
                return;
            }

            oIt->notes.push_back(OrderNote{roleToString(role), text});
            saveOrders(kOrdersFilePath, orders);

            bot.getApi().sendMessage(
                chatId,
                "📝 Заметка добавлена к заказу #" + std::to_string(targetId) + "\n\n" + formatSingleOrder(*oIt),
                nullptr,
                nullptr,
                makeKeyboardForRole(role)
            );
            return;
        }

        if (auto bIt = blockDrafts.find(userId); bIt != blockDrafts.end() && bIt->second.waitingReason) {
            if (text == kBtnCancelOrder) {
                blockDrafts.erase(userId);
                bot.getApi().sendMessage(
                    chatId,
                    "Блокировка отменена.",
                    nullptr,
                    nullptr,
                    makeKeyboardForRole(role)
                );
                return;
            }

            const int targetId = bIt->second.orderId;
            blockDrafts.erase(userId);

            auto oIt = std::find_if(orders.begin(), orders.end(), [targetId](const OrderCard& o) {
                return o.id == targetId;
            });

            if (oIt == orders.end()) {
                bot.getApi().sendMessage(
                    chatId,
                    "Заказ #" + std::to_string(targetId) + " не найден.",
                    nullptr,
                    nullptr,
                    makeKeyboardForRole(role)
                );
                return;
            }

            oIt->isBlocked = true;
            oIt->blockReason = text;
            saveOrders(kOrdersFilePath, orders);

            bot.getApi().sendMessage(
                chatId,
                "🔒 Заказ #" + std::to_string(targetId) + " заблокирован.\n\n" + formatSingleOrder(*oIt),
                nullptr,
                nullptr,
                makeKeyboardForRole(role)
            );
            return;
        }

        if (text == kBtnListOrders) {
            if (role == Admin) {
                if (orders.empty()) {
                    bot.getApi().sendMessage(
                        chatId,
                        "Список заказов пуст.",
                        nullptr,
                        nullptr,
                        makeKeyboardForRole(role)
                    );
                    return;
                }

                for (const auto& o : orders) {
                    bot.getApi().sendMessage(
                        chatId,
                        formatSingleOrder(o),
                        nullptr,
                        nullptr,
                        makeOrderInlineKeyboard(o, role)
                    );
                }
                return;
            }

            if (role == Assemble) {
                int count = 0;
                for (const auto& o : orders) {
                    if (o.status == "сборка" || o.status == "монтаж") {
                        ++count;
                        bot.getApi().sendMessage(
                            chatId,
                            formatSingleOrder(o),
                            nullptr,
                            nullptr,
                            makeOrderInlineKeyboard(o, role)
                        );
                    }
                }

                if (count == 0) {
                    bot.getApi().sendMessage(
                        chatId,
                        "Нет заказов со статусами «сборка» или «монтаж».",
                        nullptr,
                        nullptr,
                        makeKeyboardForRole(role)
                    );
                }
                return;
            }

            // Логика для ролей из getStageRules() (Sales, Project, Purchase, Production, Assemble)
            auto ruleOpt = findRuleByRole(role);
            if (!ruleOpt.has_value()) {
                return;
            }

            const StageRule& rule = *ruleOpt;
            int count = 0;

            for (const auto& o : orders) {
                if (o.status == rule.currentStatus) {
                    ++count;
                    OrderCard viewOrder = o;
                    bot.getApi().sendMessage(
                        chatId,
                        formatSingleOrder(viewOrder),
                        nullptr,
                        nullptr,
                        makeOrderInlineKeyboard(viewOrder, role)
                    );
                }
            }

            if (count == 0) {
                bot.getApi().sendMessage(
                    chatId,
                    "Нет заказов со статусом «" + rule.currentStatus + "».",
                    nullptr,
                    nullptr,
                    makeKeyboardForRole(role)
                );
            }
            return;
        }

        // Нажатие кнопки создания заказа
        if ((role == Sales || role == Admin) && text == kBtnCreateOrder) {
            OrderDraft draft;
            draft.step = OrderStep::ObjectName;
            drafts[userId] = draft;

            bot.getApi().sendMessage(
                chatId,
                "Введите имя объекта (или нажмите «Отмена» для прерывания):",
                nullptr,
                nullptr,
                makeCancelOrderKeyboard()
            );
            return;
        }

        auto it = drafts.find(userId);
        if (it == drafts.end() || it->second.step == OrderStep::None) {
            return;
        }

        if (text == kBtnCancelOrder) {
            drafts.erase(userId);
            bot.getApi().sendMessage(
                chatId,
                "Создание заказа отменено.",
                nullptr,
                nullptr,
                makeSalesKeyboard()
            );
            return;
        }

        OrderDraft& draft = it->second;

        switch (draft.step) {
            case OrderStep::ObjectName: {
                draft.objectName = text;
                draft.step = OrderStep::ClientName;
                bot.getApi().sendMessage(chatId, "Введите ФИО клиента:");
                break;
            }

            case OrderStep::ClientName: {
                draft.clientName = text;
                draft.step = OrderStep::ClientPhone;
                bot.getApi().sendMessage(chatId, "Введите номер телефона клиента:");
                break;
            }

            case OrderStep::ClientPhone: {
                draft.clientPhone = text;
                draft.step = OrderStep::Notes;
                bot.getApi().sendMessage(chatId, "Введите заметки (или «-», если заметок нет):");
                break;
            }

            case OrderStep::Notes: {
                const std::string managerName = findUserNameById(users, userId);
                std::vector<OrderNote> initialNotes;
                if (text != "-") {
                    initialNotes.push_back(OrderNote{roleToString(role), text});
                }

                OrderCard card{
                    nextOrderId++,
                    userId,
                    draft.objectName,
                    draft.clientName,
                    draft.clientPhone,
                    initialNotes,
                    "создан",
                    managerName,
                    false
                };
                orders.push_back(card);
                saveOrders(kOrdersFilePath, orders);
                drafts.erase(userId);

                bot.getApi().sendMessage(
                    chatId,
                    "Карточка заказа создана\n" + formatSingleOrder(card),
                    nullptr,
                    nullptr,
                    makeSalesKeyboard()
                );
                break;
            }

            case OrderStep::None: {
                break;
            }
        }

    });

    bot.getEvents().onCommand("users", [&bot, &users, &orders](TgBot::Message::Ptr message) {
        if (!message || !message->chat || !message->from) {
            return;
        }

        const std::int64_t chatId = message->chat->id;
        const std::int64_t userId = message->from->id;

        loadUsersFromFile(kUsersFilePath, users);
        updateOrders(kOrdersFilePath, orders);

        const auto role = getUserRole(users, userId);

        if (role != Admin) {
            bot.getApi().sendMessage(chatId, "Недостаточно прав для выполнения команды.");
            return;
        }

        if (users.empty()) {
            bot.getApi().sendMessage(chatId, "Список пользователей пока пуст.");
            return;
        }
        loadUsersFromFile(kUsersFilePath, users);
        updateOrders(kOrdersFilePath, orders);

        std::string response = "Пользователи бота (" + std::to_string(users.size()) + "):\n\n";
        for (size_t i = 0; i < users.size(); ++i) {
            const std::string line = std::to_string(i + 1) + ". " +
                                    users[i].name + " (" + std::to_string(users[i].id) + ")\n";
            // Защита от лимита длины сообщения Telegram (~4096 символов)
            if (response.size() + line.size() > 3900) {
                bot.getApi().sendMessage(chatId, response);
                response.clear();
            }
            response += line;
        }

        if (!response.empty()) {
            bot.getApi().sendMessage(chatId, response);
        }
    });

    bot.getEvents().onCommand("setrole", [&bot, &users](TgBot::Message::Ptr message) {
        if (!message || !message->chat || !message->from) {
            return;
        }

        const std::int64_t chatId = message->chat->id;
        const std::int64_t senderId = message->from->id;

        if (getUserRole(users, senderId) != Admin) {
            bot.getApi().sendMessage(chatId, "Недостаточно прав для выдачи ролей.");
            return;
        }

        std::istringstream iss(message->text.value_or(""));
        std::string cmd;
        std::int64_t targetUserId = 0;
        std::string roleArg;

        if (!(iss >> cmd >> targetUserId >> roleArg)) {
            bot.getApi().sendMessage(
                chatId,
                "Формат команды:\n"
                "/setrole <userId> <Role>\n\n"
                "Доступные роли: Admin, Sales, Project, Production, Purchase, Assemble, Marketing, Unknown"
            );
            return;
        }

        const Roles newRole = roleFromString(roleArg);
        if (newRole == Unknown && roleArg != "Unknown" && roleArg != "unknown") {
            bot.getApi().sendMessage(
                chatId,
                "Неизвестная роль «" + roleArg + "». Доступны: Admin, Sales, Project, Production, Purchase, Assemble, Marketing, Unknown."
            );
            return;
        }

        auto it = std::find_if(users.begin(), users.end(), [targetUserId](const BotUser& u) {
            return u.id == targetUserId;
        });

        if (it == users.end()) {
            bot.getApi().sendMessage(
                chatId,
                "Пользователь с ID " + std::to_string(targetUserId) + " не найден в базе. Пусть сначала нажмёт /start."
            );
            return;
        }

        it->role = newRole;
        saveUsers(kUsersFilePath, users);

        bot.getApi().sendMessage(
            chatId,
            "Роль обновлена:\n" + it->name + " (" + std::to_string(it->id) + ") → " + roleToString(it->role)
        );
    });

    bot.getEvents().onCommand("delete_order", [&bot, &orders, &users](TgBot::Message::Ptr message) {
        if (!message || !message->chat || !message->from) {
            return;
        }

        const std::int64_t chatId = message->chat->id;
        const std::int64_t senderId = message->from->id;
        const Roles senderRole = getUserRole(users, senderId);

        if (senderRole != Admin && senderRole != Sales) {
            bot.getApi().sendMessage(chatId, "Недостаточно прав для удаления заказов.");
            return;
        }

        std::istringstream iss(message->text.value_or(""));
        std::string cmd;
        int targetOrderId = 0;

        if (!(iss >> cmd >> targetOrderId) || targetOrderId <= 0) {
            bot.getApi().sendMessage(chatId, "Формат команды: /delete_order <orderId>");
            return;
        }

        auto it = std::find_if(orders.begin(), orders.end(), [targetOrderId](const OrderCard& o) {
            return o.id == targetOrderId;
        });

        if (it == orders.end()) {
            bot.getApi().sendMessage(chatId, "Заказ #" + std::to_string(targetOrderId) + " не найден.");
            return;
        }

        orders.erase(it);
        saveOrders(kOrdersFilePath, orders);

        bot.getApi().sendMessage(chatId, "Заказ #" + std::to_string(targetOrderId) + " удалён.");
    });

    bot.getEvents().onCallbackQuery([&bot, &orders, &users, &noteDrafts](TgBot::CallbackQuery::Ptr query) {
        if (!query || !query->from || !query->data.has_value()) {
            return;
        }

        const std::int64_t userId = query->from->id;
        const Roles role = getUserRole(users, userId);

        const std::string& data = *query->data;

        constexpr const char* kNotePrefix = "order_note:";
        if (data.starts_with(kNotePrefix)) {
            if (role == Unknown) {
                bot.getApi().answerCallbackQuery(query->id, "Недостаточно прав.", true);
                return;
            }

            int orderId = 0;
            try {
                orderId = std::stoi(data.substr(std::char_traits<char>::length(kNotePrefix)));
            } catch (...) {
                bot.getApi().answerCallbackQuery(query->id, "Некорректный ID заказа.", true);
                return;
            }

            auto it = std::find_if(orders.begin(), orders.end(), [orderId](const OrderCard& o) {
                return o.id == orderId;
            });

            if (it == orders.end()) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ не найден.", true);
                return;
            }

            noteDrafts[userId] = NoteDraft{true, orderId};
            bot.getApi().answerCallbackQuery(query->id, "Введите текст заметки в чат");

            if (query->message) {
                if (auto msgPtr = std::get_if<std::shared_ptr<TgBot::Message>>(&query->message->value);
                    msgPtr && *msgPtr && (*msgPtr)->chat) {
                    bot.getApi().sendMessage(
                        (*msgPtr)->chat->id,
                        "Введите заметку для заказа #" + std::to_string(orderId) + ":",
                        nullptr,
                        nullptr,
                        makeCancelOrderKeyboard()
                    );
                }
            }
            return;
        }

        constexpr const char* kPrefix = "order_done:";
        if (data.starts_with(kPrefix)) {

            int orderId = 0;
            try {
                orderId = std::stoi(data.substr(std::char_traits<char>::length(kPrefix)));
            } catch (...) {
                bot.getApi().answerCallbackQuery(query->id, "Некорректный ID заказа.", true);
                return;
            }

            auto it = std::find_if(orders.begin(), orders.end(), [orderId](const OrderCard& o) {
                return o.id == orderId;
            });

            if (it == orders.end()) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ не найден.", true);
                return;
            }

            if (it->isBlocked) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ заблокирован.", true);
                return;
            }

            std::string nextStatus;
            if (role == Admin) {
                // Админ может продвинуть заказ с любого этапа цепочки
                for (const auto& r : getStageRules()) {
                    if (r.currentStatus == it->status) {
                        nextStatus = r.nextStatus;
                        break;
                    }
                }
            } else if (auto ruleOpt = findRuleByRole(role); ruleOpt.has_value()) {
                if (it->status != ruleOpt->currentStatus) {
                    bot.getApi().answerCallbackQuery(
                        query->id,
                        "Заказ уже не в статусе «" + ruleOpt->currentStatus + "».",
                        true
                    );
                    return;
                }
                nextStatus = ruleOpt->nextStatus;
            } else {
                bot.getApi().answerCallbackQuery(query->id, "Недостаточно прав.", true);
                return;
            }

            if (nextStatus.empty()) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ уже на финальном этапе.", true);
                return;
            }

            it->status = nextStatus;
            saveOrders(kOrdersFilePath, orders);

            const std::string statusNote = "✅ Переведено в статус «" + nextStatus + "»";
            bot.getApi().answerCallbackQuery(
                query->id,
                "Заказ #" + std::to_string(orderId) + " → «" + nextStatus + "»"
            );

            if (query->message) {
                if (auto msgPtr = std::get_if<std::shared_ptr<TgBot::Message>>(&query->message->value);
                    msgPtr && *msgPtr && (*msgPtr)->chat) {
                    const auto& msg = *msgPtr;

                    TgBot::EditMessageTextArgs args;
                    args.text = formatSingleOrder(*it) + "\n\n" + statusNote;
                    args.chatId = msg->chat->id;
                    args.messageId = msg->messageId;
                    args.replyMarkup = std::make_shared<TgBot::InlineKeyboardMarkup>();
                    bot.getApi().editMessageText(args);
                }
            }
        }

        constexpr const char* kBlockPrefix = "order_block:";
        if (data.starts_with(kBlockPrefix)) {
            if (role == Unknown) {
                bot.getApi().answerCallbackQuery(query->id, "Недостаточно прав.", true);
                return;
            }

            int orderId = 0;
            try {
                orderId = std::stoi(data.substr(std::char_traits<char>::length(kBlockPrefix)));
            } catch (...) {
                bot.getApi().answerCallbackQuery(query->id, "Некорректный ID заказа.", true);
                return;
            }

            auto it = std::find_if(orders.begin(), orders.end(), [orderId](const OrderCard& o) {
                return o.id == orderId;
            });

            if (it == orders.end()) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ не найден.", true);
                return;
            }

            if (!canRoleAccessOrder(role, *it)) {
                bot.getApi().answerCallbackQuery(query->id, "Этот заказ не относится к вашему этапу.", true);
                return;
            }

            if (it->isBlocked) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ уже заблокирован.");
                return;
            }

            blockDrafts[userId] = BlockDraft{true, orderId};
            bot.getApi().answerCallbackQuery(query->id, "Введите причину блокировки в чат");

            if (query->message) {
                if (auto msgPtr = std::get_if<std::shared_ptr<TgBot::Message>>(&query->message->value);
                    msgPtr && *msgPtr && (*msgPtr)->chat) {
                    bot.getApi().sendMessage(
                        (*msgPtr)->chat->id,
                        "Введите причину блокировки для заказа #" + std::to_string(orderId) + ":",
                        nullptr,
                        nullptr,
                        makeCancelOrderKeyboard()
                    );
                }
            }
            return;
        }
    
        constexpr const char* kUnblockPrefix = "order_unblock:";
        if (data.starts_with(kUnblockPrefix)) {
            if (role == Unknown) {
                bot.getApi().answerCallbackQuery(query->id, "Недостаточно прав.", true);
                return;
            }

            int orderId = 0;
            try {
                orderId = std::stoi(data.substr(std::char_traits<char>::length(kUnblockPrefix)));
            } catch (...) {
                bot.getApi().answerCallbackQuery(query->id, "Некорректный ID заказа.", true);
                return;
            }

            auto it = std::find_if(orders.begin(), orders.end(), [orderId](const OrderCard& o) {
                return o.id == orderId;
            });

            if (it == orders.end()) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ не найден.", true);
                return;
            }

            if (!canRoleAccessOrder(role, *it)) {
                bot.getApi().answerCallbackQuery(query->id, "Этот заказ не относится к вашему этапу.", true);
                return;
            }

            if (!it->isBlocked) {
                bot.getApi().answerCallbackQuery(query->id, "Заказ уже не заблокирован.");
                return;
            }

            it->isBlocked = false;
            it->blockReason.clear();
            saveOrders(kOrdersFilePath, orders);

            const std::string statusNote = "🔓 Заказ разблокирован";
            bot.getApi().answerCallbackQuery(
                query->id,
                "Заказ #" + std::to_string(orderId) + " разблокирован"
            );

            if (query->message) {
                if (auto msgPtr = std::get_if<std::shared_ptr<TgBot::Message>>(&query->message->value);
                    msgPtr && *msgPtr && (*msgPtr)->chat) {
                    const auto& msg = *msgPtr;

                    TgBot::EditMessageTextArgs args;
                    args.text = formatSingleOrder(*it) + "\n\n" + statusNote;
                    args.chatId = msg->chat->id;
                    args.messageId = msg->messageId;
                    args.replyMarkup = makeOrderInlineKeyboard(*it, role);
                    bot.getApi().editMessageText(args);
                }
            }
            return;
        }
    
    });

    try {
        const auto me = bot.getApi().getMe();
        std::cout << "Username бота: " << me->username.value_or("<no username>") << "\n";
        TgBot::TgLongPoll longPoll(bot);
        while (true) {
            longPoll.start();
        }
    } catch (const std::exception& e) {
        std::cerr << "Ошибка: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
