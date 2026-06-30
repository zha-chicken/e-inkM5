#ifndef EINK_M5_MEMO_STORE_H
#define EINK_M5_MEMO_STORE_H

#include <string>

class MemoStore {
public:
    static std::string HandleMcpAction(const std::string& action,
                                       int id,
                                       const std::string& title,
                                       const std::string& content);
    static std::string DisplayText();
};

#endif  // EINK_M5_MEMO_STORE_H
