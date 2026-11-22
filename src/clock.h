#pragma once

#include <vector>

#include "model.h"

ClockModel buildClockModel(const Config& config);
Json clockModelToJson(const ClockModel& model);
Json observationToJson(const CaptureObservation& observation);
int64_t referenceTimeForSequence(const Config& config, int sequence);

