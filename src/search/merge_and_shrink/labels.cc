#include "labels.h"

#include "label.h"
#include "transition_system.h"

#include "../equivalence_relation.h"
#include "../globals.h"
#include "../option_parser.h"
//#include "../utilities.h"

#include <algorithm>
#include <cassert>
//#include <iostream>
//#include <limits>

using namespace std;

Labels::Labels(const Options &options, OperatorCost cost_type)
    : label_reduction_method(
          LabelReductionMethod(options.get_enum("label_reduction_method"))),
      label_reduction_system_order(
          LabelReductionSystemOrder(options.get_enum("label_reduction_system_order"))) {
    // Generate a label for every operator
    if (!g_operators.empty()) {
        if (g_operators.size() * 2 - 1 > labels.max_size())
            exit_with(EXIT_OUT_OF_MEMORY);
        labels.reserve(g_operators.size() * 2 - 1);
    }
    for (size_t i = 0; i < g_operators.size(); ++i) {
        labels.push_back(new OperatorLabel(i, get_adjusted_action_cost(g_operators[i], cost_type),
                                           g_operators[i].get_preconditions(),
                                           g_operators[i].get_effects()));
    }

    // Compute the transition system order
    size_t max_no_systems = g_variable_domain.size() * 2 - 1;
    transition_system_order.reserve(max_no_systems);
    if (label_reduction_system_order == REGULAR
        || label_reduction_system_order == RANDOM) {
        for (size_t i = 0; i < max_no_systems; ++i)
            transition_system_order.push_back(i);
        if (label_reduction_system_order == RANDOM) {
            random_shuffle(transition_system_order.begin(), transition_system_order.end());
        }
    } else {
        assert(label_reduction_system_order == REVERSE);
        for (size_t i = 0; i < max_no_systems; ++i)
            transition_system_order.push_back(max_no_systems - 1 - i);
    }
}

void Labels::notify_transition_systems(
    int ts_index,
    const vector<TransitionSystem *> &all_transition_systems,
    const vector<pair<int, vector<int> > > &label_mapping,
    vector<EquivalenceRelation *> &cached_equivalence_relations) const {
    for (size_t i = 0; i < all_transition_systems.size(); ++i) {
        if (all_transition_systems[i]) {
            all_transition_systems[i]->apply_label_reduction(label_mapping,
                                                             static_cast<int>(i) != ts_index);
            if (cached_equivalence_relations[i]) {
                delete cached_equivalence_relations[i];
                cached_equivalence_relations[i] = 0;
            }
        }
    }
}

bool Labels::apply_label_reduction(const EquivalenceRelation *relation,
                                   vector<pair<int, vector<int> > > &label_mapping) {
    int num_labels = 0;
    int num_labels_after_reduction = 0;
    for (BlockListConstIter it = relation->begin(); it != relation->end(); ++it) {
        const Block &block = *it;
        vector<Label *> equivalent_labels;
        vector<int> equivalent_label_nos;
        for (ElementListConstIter jt = block.begin(); jt != block.end(); ++jt) {
            assert(*jt < static_cast<int>(labels.size()));
            Label *label = labels[*jt];
            if (!label->is_reduced()) {
                // only consider non-reduced labels
                equivalent_labels.push_back(label);
                equivalent_label_nos.push_back(*jt);
                ++num_labels;
            }
        }
        if (equivalent_labels.size() > 1) {
            Label *new_label = new CompositeLabel(labels.size(), equivalent_labels);
            labels.push_back(new_label);
            label_mapping.push_back(make_pair(new_label->get_id(), equivalent_label_nos));
        }
        if (!equivalent_labels.empty()) {
            ++num_labels_after_reduction;
        }
    }
    int number_reduced_labels = num_labels - num_labels_after_reduction;
    if (number_reduced_labels > 0) {
        cout << "Label reduction: "
             << num_labels << " labels, "
             << num_labels_after_reduction << " after reduction"
             << endl;
    }
    return number_reduced_labels;
}

EquivalenceRelation *Labels::compute_combinable_equivalence_relation(
    int ts_index,
    const vector<TransitionSystem *> &all_transition_systems,
    vector<EquivalenceRelation *> &cached_local_equivalence_relations) const {
    /*
      Returns an equivalence relation over labels s.t. l ~ l'
      iff l and l' are locally equivalent in all transition systems
      T' \neq T. (They may or may not be locally equivalent in T.)
    */
    TransitionSystem *transition_system = all_transition_systems[ts_index];
    assert(transition_system);
    //cout << transition_system->tag() << "compute combinable labels" << endl;

    // create the equivalence relation where all labels are equivalent
    int num_labels = labels.size();
    vector<pair<int, int> > annotated_labels;
    annotated_labels.reserve(num_labels);
    for (int label_no = 0; label_no < num_labels; ++label_no) {
        const Label *label = labels[label_no];
        assert(label->get_id() == label_no);
        if (!label->is_reduced()) {
            // only consider non-reduced labels
            annotated_labels.push_back(make_pair(0, label_no));
        }
    }
    EquivalenceRelation *relation =
        EquivalenceRelation::from_annotated_elements<int>(num_labels, annotated_labels);

    for (size_t i = 0; i < all_transition_systems.size(); ++i) {
        TransitionSystem *ts = all_transition_systems[i];
        if (!ts || ts == transition_system) {
            continue;
        }
        if (!cached_local_equivalence_relations[i]) {
            cached_local_equivalence_relations[i] = ts->compute_local_equivalence_relation();
        }
        relation->refine(*cached_local_equivalence_relations[i]);
    }
    return relation;
}

void Labels::reduce(pair<int, int> next_merge,
                    const vector<TransitionSystem *> &all_transition_systems) {
    int num_transition_systems = all_transition_systems.size();

    if (label_reduction_method == NONE) {
        return;
    }

    if (label_reduction_method == TWO_TRANSITION_SYSTEMS) {
        /* Note:
           We compute the combinable relation for labels for the two transition systems
           in the order given by the merge strategy. We conducted experiments
           testing the impact of always starting with the larger transitions system
           (in terms of variables) or with the smaller transition system and found
           no significant differences.
         */
        assert(all_transition_systems[next_merge.first]);
        assert(all_transition_systems[next_merge.second]);

        vector<EquivalenceRelation *> cached_local_equivalence_relations(
            all_transition_systems.size(), 0);

        EquivalenceRelation *relation = compute_combinable_equivalence_relation(
            next_merge.first,
            all_transition_systems,
            cached_local_equivalence_relations);
        vector<pair<int, vector<int> > > label_mapping;
        bool have_reduced = apply_label_reduction(relation, label_mapping);
        if (have_reduced) {
            notify_transition_systems(next_merge.first,
                                      all_transition_systems,
                                      label_mapping,
                                      cached_local_equivalence_relations);
        }
        delete relation;
        label_mapping.clear();

        relation = compute_combinable_equivalence_relation(
            next_merge.second,
            all_transition_systems,
            cached_local_equivalence_relations);
        have_reduced = apply_label_reduction(relation, label_mapping);
        if (have_reduced) {
            notify_transition_systems(next_merge.second,
                                      all_transition_systems,
                                      label_mapping,
                                      cached_local_equivalence_relations);
        }
        delete relation;

        for (size_t i = 0; i < cached_local_equivalence_relations.size(); ++i)
            delete cached_local_equivalence_relations[i];
        return;
    }

    // Make sure that we start with an index not ouf of range for
    // all_transition_systems
    size_t tso_index = 0;
    assert(!transition_system_order.empty());
    while (transition_system_order[tso_index] >= num_transition_systems) {
        ++tso_index;
        assert(in_bounds(tso_index, transition_system_order));
    }

    int max_iterations;
    if (label_reduction_method == ALL_TRANSITION_SYSTEMS) {
        max_iterations = num_transition_systems;
    } else if (label_reduction_method == ALL_TRANSITION_SYSTEMS_WITH_FIXPOINT) {
        max_iterations = numeric_limits<int>::max();
    } else {
        ABORT("unknown label reduction method");
    }

    int num_unsuccessful_iterations = 0;
    vector<EquivalenceRelation *> cached_local_equivalence_relations(
        all_transition_systems.size(), 0);

    for (int i = 0; i < max_iterations; ++i) {
        int ts_index = transition_system_order[tso_index];
        TransitionSystem *current_transition_system = all_transition_systems[ts_index];

        bool have_reduced = false;
        vector<pair<int, vector<int> > > label_mapping;
        if (current_transition_system != 0) {
            EquivalenceRelation *relation =
                compute_combinable_equivalence_relation(ts_index,
                                                        all_transition_systems,
                                                        cached_local_equivalence_relations);
            have_reduced = apply_label_reduction(relation, label_mapping);
            delete relation;
        }

        if (have_reduced) {
            num_unsuccessful_iterations = 0;
            notify_transition_systems(ts_index,
                                      all_transition_systems,
                                      label_mapping,
                                      cached_local_equivalence_relations);
        } else {
            // Even if the transition system has been removed, we need to count
            // it as unsuccessful iterations (the size of the vector matters).
            ++num_unsuccessful_iterations;
        }
        if (num_unsuccessful_iterations == num_transition_systems - 1)
            break;

        ++tso_index;
        if (tso_index == transition_system_order.size()) {
            tso_index = 0;
        }
        while (transition_system_order[tso_index] >= num_transition_systems) {
            ++tso_index;
            if (tso_index == transition_system_order.size()) {
                tso_index = 0;
            }
        }
    }

    for (size_t i = 0; i < cached_local_equivalence_relations.size(); ++i)
        delete cached_local_equivalence_relations[i];
}

const Label *Labels::get_label_by_index(int index) const {
    assert(in_bounds(index, labels));
    return labels[index];
}

bool Labels::is_label_reduced(int label_no) const {
    return get_label_by_index(label_no)->is_reduced();
}

void Labels::dump_labels() const {
    cout << "no of labels: " << labels.size() << endl;
    for (size_t i = 0; i < labels.size(); ++i) {
        const Label *label = labels[i];
        label->dump();
    }
}

void Labels::dump_label_reduction_options() const {
    cout << "Label reduction: ";
    switch (label_reduction_method) {
    case NONE:
        cout << "disabled";
        break;
    case TWO_TRANSITION_SYSTEMS:
        cout << "two transition systems (which will be merged next)";
        break;
    case ALL_TRANSITION_SYSTEMS:
        cout << "all transition systems";
        break;
    case ALL_TRANSITION_SYSTEMS_WITH_FIXPOINT:
        cout << "all transition systems with fixpoint computation";
        break;
    }
    cout << endl;
    if (label_reduction_method == ALL_TRANSITION_SYSTEMS ||
        label_reduction_method == ALL_TRANSITION_SYSTEMS_WITH_FIXPOINT) {
        cout << "System order: ";
        switch (label_reduction_system_order) {
        case REGULAR:
            cout << "regular";
            break;
        case REVERSE:
            cout << "reversed";
            break;
        case RANDOM:
            cout << "random";
            break;
        }
        cout << endl;
    }
}
