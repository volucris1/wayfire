#include <wayfire/scene.hpp>
#include <wayfire/view.hpp>
#include <set>
#include <algorithm>

#include "scene-priv.hpp"

namespace wf
{
namespace scene
{
node_t::~node_t()
{}

node_t::node_t(bool is_structure)
{
    this->_is_structure = is_structure;
}

inner_node_t::inner_node_t(bool _is_structure) : node_t(_is_structure)
{}

std::optional<input_node_t> inner_node_t::find_node_at(const wf::pointf_t& at)
{
    for (auto& node : get_children())
    {
        auto child_node = node->find_node_at(at);
        if (child_node.has_value())
        {
            return child_node;
        }
    }

    return {};
}

iteration inner_node_t::visit(visitor_t *visitor)
{
    auto proceed = visitor->inner_node(this);
    switch (proceed)
    {
      case iteration::STOP:
        // Tell parent to stop
        return iteration::STOP;

      case iteration::ALL:
        // Go through all children and see what they want
        for (auto& ch : get_children())
        {
            if (ch->visit(visitor) == iteration::STOP)
            {
                return iteration::STOP;
            }
        }

      // fallthrough

      case iteration::SKIP_CHILDREN:
        return iteration::ALL;
    }

    assert(false);
}

static std::vector<node_t*> extract_structure_nodes(
    const std::vector<node_ptr>& list)
{
    std::vector<node_t*> structure;
    for (auto& node : list)
    {
        if (node->is_structure_node())
        {
            structure.push_back(node.get());
        }
    }

    return structure;
}

bool floating_inner_node_t::set_children_list(std::vector<node_ptr> new_list)
{
    // Structure nodes should be sorted in both sequences and be the same.
    // For simplicity, we just extract the nodes in new vectors and check that
    // they are the same.
    //
    // FIXME: this could also be done with a merge-sort-like algorithm in place,
    // but is it worth it here? The scenegraph is supposed to stay static for
    // most of the time.
    if (extract_structure_nodes(children) != extract_structure_nodes(new_list))
    {
        return false;
    }

    set_children_unchecked(std::move(new_list));
    return true;
}

void inner_node_t::set_children_unchecked(std::vector<node_ptr> new_list)
{
    for (auto& node : new_list)
    {
        node->_parent = this;
    }

    this->children = std::move(new_list);
}

// FIXME: output nodes are actually structure nodes, but we need to add and
// remove them dynamically ...
output_node_t::output_node_t() : inner_node_t(false)
{
    this->_static = std::make_shared<floating_inner_node_t>(true);
    this->dynamic = std::make_shared<floating_inner_node_t>(true);
    set_children_unchecked({dynamic, _static});
}

root_node_t::root_node_t() : floating_inner_node_t(true)
{
    std::vector<node_ptr> children;

    for (int i = (int)layer::ALL_LAYERS - 1; i >= 0; i--)
    {
        layers[i] = std::make_shared<floating_inner_node_t>(true);
        children.push_back(layers[i]);
    }

    set_children_unchecked(children);
    this->priv = std::make_unique<root_node_t::priv_t>();
}

root_node_t::~root_node_t()
{}

void root_node_t::update()
{
    priv->update_active_nodes(this);
}

class collect_active_nodes_t final : public visitor_t
{
  public:
    std::vector<node_ptr> active_nodes;
    void try_push(node_t *node)
    {
        if (node->flags() & (int)node_flags::ACTIVE_KEYBOARD)
        {
            active_nodes.push_back(node->shared_from_this());
        }
    }

    /** Visit an inner node with children. */
    iteration inner_node(inner_node_t *node) final
    {
        try_push(node);
        return iteration::ALL;
    }

    /** Visit a view node. */
    iteration view_node(view_node_t *node) final
    {
        try_push(node);
        return iteration::ALL;
    }

    /** Visit a generic node whose type is neither inner nor view. */
    iteration generic_node(node_t *node) final
    {
        try_push(node);
        return iteration::ALL;
    }
};

void root_node_t::priv_t::update_active_nodes(root_node_t *root)
{
    collect_active_nodes_t collector;
    root->visit(&collector);

    std::set<node_ptr> already_focused{
        active_keyboard_nodes.begin(),
        active_keyboard_nodes.end()
    };

    std::set<node_ptr> new_focused{
        collector.active_nodes.begin(),
        collector.active_nodes.end()
    };

    for (auto& old_focus : already_focused)
    {
        if (!new_focused.count(old_focus))
        {
            old_focus->keyboard_interaction().handle_keyboard_leave();
        }
    }

    for (auto& new_focus : new_focused)
    {
        if (!already_focused.count(new_focus))
        {
            new_focus->keyboard_interaction().handle_keyboard_enter();
        }
    }

    this->active_keyboard_nodes = std::move(collector.active_nodes);
}

void root_node_t::priv_t::handle_key(wlr_event_keyboard_key ev)
{
    for (auto& node : this->active_keyboard_nodes)
    {
        auto result = node->keyboard_interaction().handle_keyboard_key(ev);
        if (result == keyboard_action::CONSUME)
        {
            break;
        }
    }
}
} // namespace scene
}
