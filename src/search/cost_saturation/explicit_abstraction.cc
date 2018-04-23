#include "explicit_abstraction.h"

#include "types.h"

#include "../utils/collections.h"
#include "../utils/logging.h"

#include <unordered_set>

using namespace std;

namespace cost_saturation {
static void dijkstra_search(
    const vector<vector<Successor>> &graph,
    priority_queues::AdaptiveQueue<int> &queue,
    vector<int> &distances,
    const vector<int> &costs) {
    while (!queue.empty()) {
        pair<int, int> top_pair = queue.pop();
        int distance = top_pair.first;
        int state = top_pair.second;
        int state_distance = distances[state];
        assert(state_distance <= distance);
        if (state_distance < distance) {
            continue;
        }
        for (const Successor &transition : graph[state]) {
            int successor = transition.state;
            int op = transition.op;
            assert(utils::in_bounds(op, costs));
            int cost = costs[op];
            assert(cost >= 0);
            int successor_distance = (cost == INF) ? INF : state_distance + cost;
            assert(successor_distance >= 0);
            if (distances[successor] > successor_distance) {
                distances[successor] = successor_distance;
                queue.push(successor_distance, successor);
            }
        }
    }
}

ostream &operator<<(ostream &os, const Successor &successor) {
    os << "(" << successor.op << ", " << successor.state << ")";
    return os;
}

static vector<int> get_active_operators_from_graph(
    const vector<vector<Successor>> &backward_graph) {
    unordered_set<int> active_operators;
    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            assert(transition.state != target);
            active_operators.insert(op_id);
        }
    }
    vector<int> active_operators_sorted(active_operators.begin(), active_operators.end());
    sort(active_operators_sorted.begin(), active_operators_sorted.end());
    return active_operators_sorted;
}

ExplicitAbstraction::ExplicitAbstraction(
    AbstractionFunction function,
    vector<vector<Successor>> &&backward_graph,
    vector<int> &&looping_operators,
    vector<int> &&goal_states)
    : abstraction_function(function),
      backward_graph(move(backward_graph)),
      looping_operators(move(looping_operators)),
      goal_states(move(goal_states)) {
#ifndef NDEBUG
    for (int target = 0; target < get_num_states(); ++target) {
        vector<Successor> copied_transitions = this->backward_graph[target];
        sort(copied_transitions.begin(), copied_transitions.end());
        assert(utils::is_sorted_unique(copied_transitions));
    }
#endif
}

vector<int> ExplicitAbstraction::compute_h_values(const vector<int> &costs) const {
    vector<int> goal_distances(get_num_states(), INF);
    queue.clear();
    for (int goal_state : goal_states) {
        goal_distances[goal_state] = 0;
        queue.push(0, goal_state);
    }
    dijkstra_search(backward_graph, queue, goal_distances, costs);
    return goal_distances;
}

vector<Transition> ExplicitAbstraction::get_transitions() const {
    assert(has_transition_system());
    vector<Transition> transitions;
    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            int src = transition.state;
            assert(src != target);
            transitions.emplace_back(src, op_id, target);
        }
    }
    return transitions;
}

vector<int> ExplicitAbstraction::compute_saturated_costs(
    const vector<int> &h_values,
    int num_operators,
    bool use_general_costs) const {
    const int min_cost = use_general_costs ? -INF : 0;

    vector<int> saturated_costs(num_operators, min_cost);

    /* To prevent negative cost cycles we ensure that all operators
       inducing self-loops have non-negative costs. */
    if (use_general_costs) {
        for (int op_id : looping_operators) {
            saturated_costs[op_id] = 0;
        }
    }

    int num_states = backward_graph.size();
    for (int target = 0; target < num_states; ++target) {
        assert(utils::in_bounds(target, h_values));
        int target_h = h_values[target];
        if (target_h == INF) {
            continue;
        }

        for (const Successor &transition : backward_graph[target]) {
            int op_id = transition.op;
            int src = transition.state;
            assert(src != target);
            assert(utils::in_bounds(src, h_values));
            int src_h = h_values[src];
            if (src_h == INF) {
                continue;
            }

            const int needed = src_h - target_h;
            saturated_costs[op_id] = max(saturated_costs[op_id], needed);
        }
    }
    return saturated_costs;
}

int ExplicitAbstraction::get_num_states() const {
    assert(has_transition_system());
    return backward_graph.size();
}

int ExplicitAbstraction::get_abstract_state_id(const State &concrete_state) const {
    return abstraction_function(concrete_state);
}

vector<int> ExplicitAbstraction::compute_active_operators() const {
    assert(has_transition_system());
    return get_active_operators_from_graph(backward_graph);
}

const vector<int> &ExplicitAbstraction::get_looping_operators() const {
    assert(has_transition_system());
    return looping_operators;
}

const vector<int> &ExplicitAbstraction::get_goal_states() const {
    assert(has_transition_system());
    return goal_states;
}

void ExplicitAbstraction::release_transition_system_memory() {
    utils::release_vector_memory(looping_operators);
    utils::release_vector_memory(goal_states);
    utils::release_vector_memory(backward_graph);
}

void ExplicitAbstraction::dump() const {
    assert(has_transition_system());
    cout << "State-changing transitions:" << endl;
    for (size_t state = 0; state < backward_graph.size(); ++state) {
        if (!backward_graph[state].empty()) {
            cout << "  " << state << " <- " << backward_graph[state] << endl;
        }
    }
    cout << "Looping operators: " << looping_operators << endl;
    cout << "Goal states: " << goal_states << endl;
}
}