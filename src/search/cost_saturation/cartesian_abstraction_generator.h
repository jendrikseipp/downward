#ifndef COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H
#define COST_SATURATION_CARTESIAN_ABSTRACTION_GENERATOR_H

#include "abstraction_generator.h"

namespace options {
class Options;
}

namespace cegar {
class SubtaskGenerator;
}

namespace utils {
class CountdownTimer;
class RandomNumberGenerator;
}

namespace cost_saturation {
class CartesianAbstractionGenerator : public AbstractionGenerator {
    const std::vector<std::shared_ptr<cegar::SubtaskGenerator>> subtask_generators;
    const int max_transitions;
    const std::shared_ptr<utils::RandomNumberGenerator> rng;

    int num_transitions;

    void build_abstractions_for_subtasks(
        const std::vector<std::shared_ptr<AbstractTask>> &subtasks,
        std::function<bool()> total_size_limit_reached,
        Abstractions &abstractions);

public:
    explicit CartesianAbstractionGenerator(const options::Options &opts);

    Abstractions generate_abstractions(
        const std::shared_ptr<AbstractTask> &task);
};
}

#endif