#pragma once

#include "block.hpp"
#include <string>
#include <vector>
#include <map>

namespace Sanjeev {

struct AuthorityNode {
    std::string address;
    std::string name;
    std::string public_key_pem;
    bool is_active = true;
};

class PoAConsensus {
public:
    PoAConsensus() = default;

    void register_authority(const std::string& address, const std::string& name, const std::string& public_key_pem);
    void revoke_authority(const std::string& address);
    bool is_authorized(const std::string& address) const;
    const AuthorityNode* get_authority(const std::string& address) const;
    std::vector<AuthorityNode> get_all_authorities() const;

    bool validate_block_header(const Block& block, const Block* prev_block) const;

private:
    std::map<std::string, AuthorityNode> authorities_;
};

} // namespace Sanjeev
