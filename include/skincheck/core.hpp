#pragma once

#include <vector>

#include "skincheck/types.hpp"

namespace skincheck {

Capture make_demo_capture();
Timeline build_timeline(const Capture& capture, int min_gap_frames);
ResourcePlan plan_resources(const Capture& capture, const Timeline& timeline,
                            const ResourcePolicy& policy);
FrameResult check_frame(const Timeline& timeline, int frame, const ResourcePlan& plan);
std::vector<FrameResult> run_frame_range(const Timeline& timeline, int first_frame,
                                         int last_frame, const ResourcePlan& plan);
Summary summarize(const std::vector<FrameResult>& frames);
ResumeComparison compare_resume_results(const Timeline& timeline, const ResourcePlan& plan,
                                        const std::vector<FrameResult>& checkpoint_frames,
                                        const std::vector<FrameResult>& final_frames);

}  // namespace skincheck

