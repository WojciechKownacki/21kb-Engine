// The crowd update of the engine's DetourCrowd build (see the root CMakeLists.txt): this file replaces
// the upstream dtCrowd::checkPathValidity and dtCrowd::update at the end of a generated copy of
// DetourCrowd.cpp and is compiled only there. The passes over members are the upstream ones, in the
// same order and with the same arithmetic. The passes in which every member reads the others but
// writes only itself (path validity, wall and neighbour search, corners, steering, velocity
// sampling, integration, collision push, moving along the surface) run through the parallel-for set
// with setParallelFor, each worker with its own Detour
// query objects. Path requests, topology optimisation, off-mesh links and every write to another
// member stay on the calling thread, so the result does not depend on the number of workers.

#include <chrono>

namespace {

struct dtCrowdPassClock
{
	std::chrono::steady_clock::time_point mark = std::chrono::steady_clock::now();

	float lap()
	{
		const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
		const float elapsed = std::chrono::duration<float, std::milli>(now - mark).count();
		mark = now;
		return elapsed;
	}
};

}

void dtCrowd::releaseWorkers()
{
	for (int i = 0; i < m_workerCount; ++i)
	{
		if (m_workerQueries)
			dtFreeNavMeshQuery(m_workerQueries[i]);
		if (m_workerAvoidance)
			dtFreeObstacleAvoidanceQuery(m_workerAvoidance[i]);
	}
	dtFree(m_workerQueries);
	dtFree(m_workerAvoidance);
	dtFree(m_workerSamples);
	m_workerQueries = 0;
	m_workerAvoidance = 0;
	m_workerSamples = 0;
	m_workerCount = 0;
	m_parallelFor = 0;
	m_parallelUser = 0;
}

bool dtCrowd::setParallelFor(dtCrowdParallelFor run, void* user, const int workers)
{
	releaseWorkers();
	if (!run || workers <= 0)
		return true;
	if (!m_navquery)
		return false;
	const size_t count = (size_t)workers;
	m_workerQueries = (dtNavMeshQuery**)dtAlloc(sizeof(dtNavMeshQuery*) * count, DT_ALLOC_PERM);
	m_workerAvoidance = (dtObstacleAvoidanceQuery**)dtAlloc(sizeof(dtObstacleAvoidanceQuery*) * count, DT_ALLOC_PERM);
	m_workerSamples = (int*)dtAlloc(sizeof(int) * count, DT_ALLOC_PERM);
	if (!m_workerQueries || !m_workerAvoidance || !m_workerSamples)
	{
		releaseWorkers();
		return false;
	}
	memset(m_workerQueries, 0, sizeof(dtNavMeshQuery*) * count);
	memset(m_workerAvoidance, 0, sizeof(dtObstacleAvoidanceQuery*) * count);
	memset(m_workerSamples, 0, sizeof(int) * count);
	m_workerCount = workers;
	for (int i = 0; i < workers; ++i)
	{
		// Each worker's queries are sized like the crowd's own.
		m_workerQueries[i] = dtAllocNavMeshQuery();
		m_workerAvoidance[i] = dtAllocObstacleAvoidanceQuery();
		if (!m_workerQueries[i] || dtStatusFailed(m_workerQueries[i]->init(m_navquery->getAttachedNavMesh(), MAX_COMMON_NODES)) ||
			!m_workerAvoidance[i] || !m_workerAvoidance[i]->init(6, 8))
		{
			releaseWorkers();
			return false;
		}
	}
	m_parallelFor = run;
	m_parallelUser = user;
	return true;
}

template <typename Pass>
void dtCrowd::runPass(const int count, Pass& pass)
{
	if (count <= 0)
		return;
	if (!m_parallelFor || m_workerCount <= 0)
	{
		pass(0, count, -1);
		return;
	}
	m_parallelFor(m_parallelUser, count, [](void* context, const int begin, const int end, const int worker) {
		(*static_cast<Pass*>(context))(begin, end, worker);
	}, &pass);
}

// Every walking member checks that it still stands on the mesh, that its target still exists and
// that the next polygons of its path are still passable, and asks for a new path when not.
void dtCrowd::checkPathValidity(dtCrowdAgent** agents, const int nagents, const float dt)
{
	static const int CHECK_LOOKAHEAD = 10;
	static const float TARGET_REPLAN_DELAY = 1.0f;
	auto check = [&](const int begin, const int end, const int worker) {
		dtNavMeshQuery* query = worker < 0 ? m_navquery : m_workerQueries[worker];
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING)
				continue;
			const dtQueryFilter* filter = &m_filters[ag->params.queryFilterType];
			ag->targetReplanTime += dt;
			bool replan = false;
			float agentPos[3];
			dtVcopy(agentPos, ag->npos);
			dtPolyRef agentRef = ag->corridor.getFirstPoly();
			if (!query->isValidPolyRef(agentRef, filter))
			{
				// The polygon under the member went away: move it to the nearest one, or drop it.
				float nearest[3];
				dtVcopy(nearest, agentPos);
				agentRef = 0;
				query->findNearestPoly(ag->npos, m_agentPlacementHalfExtents, filter, &agentRef, nearest);
				dtVcopy(agentPos, nearest);
				if (!agentRef)
				{
					ag->corridor.reset(0, agentPos);
					ag->partial = false;
					ag->boundary.reset();
					ag->state = DT_CROWDAGENT_STATE_INVALID;
					continue;
				}
				// Only the start is fixed; the rest of the path helps the new plan.
				ag->corridor.fixPathStart(agentRef, agentPos);
				ag->boundary.reset();
				dtVcopy(ag->npos, agentPos);
				replan = true;
			}
			if (ag->targetState == DT_CROWDAGENT_TARGET_NONE || ag->targetState == DT_CROWDAGENT_TARGET_VELOCITY)
				continue;
			if (ag->targetState != DT_CROWDAGENT_TARGET_FAILED)
			{
				if (!query->isValidPolyRef(ag->targetRef, filter))
				{
					// The target's polygon went away: move the target to the nearest one.
					float nearest[3];
					dtVcopy(nearest, ag->targetPos);
					ag->targetRef = 0;
					query->findNearestPoly(ag->targetPos, m_agentPlacementHalfExtents, filter, &ag->targetRef, nearest);
					dtVcopy(ag->targetPos, nearest);
					replan = true;
				}
				if (!ag->targetRef)
				{
					ag->corridor.reset(agentRef, agentPos);
					ag->partial = false;
					ag->targetState = DT_CROWDAGENT_TARGET_NONE;
				}
			}
			if (!ag->corridor.isValid(CHECK_LOOKAHEAD, query, filter))
				replan = true;
			// A short path that ends short of the target is planned again after a while.
			if (ag->targetState == DT_CROWDAGENT_TARGET_VALID && ag->targetReplanTime > TARGET_REPLAN_DELAY &&
				ag->corridor.getPathCount() < CHECK_LOOKAHEAD && ag->corridor.getLastPoly() != ag->targetRef)
				replan = true;
			if (replan && ag->targetState != DT_CROWDAGENT_TARGET_NONE)
				requestMoveTargetReplan(getAgentIndex(ag), ag->targetRef, ag->targetPos);
		}
	};
	runPass(nagents, check);
}

void dtCrowd::update(const float dt, dtCrowdAgentDebugInfo* debug)
{
	dtCrowdPassClock clock;
	m_velocitySampleCount = 0;

	const int debugIdx = debug ? debug->idx : -1;

	dtCrowdAgent** agents = m_activeAgents;
	const int nagents = getActiveAgents(agents, m_maxAgents);

	// Worker -1 is the calling thread, which uses the crowd's own query objects.
	const auto queryOf = [this](const int worker) { return worker < 0 ? m_navquery : m_workerQueries[worker]; };

	// Paths: validity, requests and the path queue, topology optimisation.
	checkPathValidity(agents, nagents, dt);
	updateMoveRequest(dt);
	updateTopologyOptimization(agents, nagents, dt);
	m_passMilliseconds[DT_CROWD_PASS_PATHS] = clock.lap();

	// The proximity grid holds every member once per update; then each member gathers its walls and
	// its nearest neighbours.
	m_grid->clear();
	for (int i = 0; i < nagents; ++i)
	{
		const float* p = agents[i]->npos;
		const float r = agents[i]->params.radius;
		m_grid->addItem((unsigned short)i, p[0] - r, p[2] - r, p[0] + r, p[2] + r);
	}
	auto gatherNeighbours = [&](const int begin, const int end, const int worker) {
		dtNavMeshQuery* query = queryOf(worker);
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING)
				continue;
			const dtQueryFilter* filter = &m_filters[ag->params.queryFilterType];
			// The walls are gathered again once the member has moved a quarter of its range or they changed.
			const float refreshDistance = ag->params.collisionQueryRange * 0.25f;
			if (dtVdist2DSqr(ag->npos, ag->boundary.getCenter()) > dtSqr(refreshDistance) || !ag->boundary.isValid(query, filter))
				ag->boundary.update(ag->corridor.getFirstPoly(), ag->npos, ag->params.collisionQueryRange, query, filter);
			ag->nneis = getNeighbours(ag->npos, ag->params.height, ag->params.collisionQueryRange, ag, ag->neis,
									  DT_CROWDAGENT_MAX_NEIGHBOURS, agents, nagents, m_grid);
			for (int j = 0; j < ag->nneis; ++j)
				ag->neis[j].idx = getAgentIndex(agents[ag->neis[j].idx]);
		}
	};
	runPass(nagents, gatherNeighbours);
	m_passMilliseconds[DT_CROWD_PASS_NEIGHBOURS] = clock.lap();

	// The corners each member steers through, its path shortened toward the second one when visible.
	auto findCorners = [&](const int begin, const int end, const int worker) {
		dtNavMeshQuery* query = queryOf(worker);
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING || ag->targetState == DT_CROWDAGENT_TARGET_NONE ||
				ag->targetState == DT_CROWDAGENT_TARGET_VELOCITY)
				continue;
			const dtQueryFilter* filter = &m_filters[ag->params.queryFilterType];
			ag->ncorners = ag->corridor.findCorners(ag->cornerVerts, ag->cornerFlags, ag->cornerPolys, DT_CROWDAGENT_MAX_CORNERS, query, filter);
			const bool shorten = (ag->params.updateFlags & DT_CROWD_OPTIMIZE_VIS) && ag->ncorners > 0;
			const float* target = shorten ? &ag->cornerVerts[dtMin(1, ag->ncorners - 1) * 3] : 0;
			if (shorten)
				ag->corridor.optimizePathVisibility(target, ag->params.pathOptimizationRange, query, filter);
			if (debugIdx == i)
			{
				if (shorten)
				{
					dtVcopy(debug->optStart, ag->corridor.getPos());
					dtVcopy(debug->optEnd, target);
				}
				else
				{
					dtVset(debug->optStart, 0, 0, 0);
					dtVset(debug->optEnd, 0, 0, 0);
				}
			}
		}
	};
	runPass(nagents, findCorners);

	// Members whose next corner is an off-mesh connection start crossing it.
	for (int i = 0; i < nagents; ++i)
	{
		dtCrowdAgent* ag = agents[i];
		if (ag->state != DT_CROWDAGENT_STATE_WALKING || ag->targetState == DT_CROWDAGENT_TARGET_NONE ||
			ag->targetState == DT_CROWDAGENT_TARGET_VELOCITY)
			continue;
		if (!overOffmeshConnection(ag, ag->params.radius * 2.25f))
			continue;
		dtCrowdAgentAnimation* anim = &m_agentAnims[getAgentIndex(ag)];
		dtPolyRef refs[2];
		// A connection that cannot be entered is replanned by the path validity check.
		if (!ag->corridor.moveOverOffmeshConnection(ag->cornerPolys[ag->ncorners - 1], refs, anim->startPos, anim->endPos, m_navquery))
			continue;
		dtVcopy(anim->initPos, ag->npos);
		anim->polyRef = refs[1];
		anim->active = true;
		anim->t = 0.0f;
		anim->tmax = (dtVdist2D(anim->startPos, anim->endPos) / ag->params.maxSpeed) * 0.5f;
		ag->state = DT_CROWDAGENT_STATE_OFFMESH;
		ag->ncorners = 0;
		ag->nneis = 0;
	}
	m_passMilliseconds[DT_CROWD_PASS_CORNERS] = clock.lap();

	// Desired velocities: toward the corners, slowing near the goal, pushed apart from neighbours.
	auto steer = [&](const int begin, const int end, const int) {
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING || ag->targetState == DT_CROWDAGENT_TARGET_NONE)
				continue;
			float dvel[3] = { 0, 0, 0 };
			if (ag->targetState == DT_CROWDAGENT_TARGET_VELOCITY)
			{
				dtVcopy(dvel, ag->targetPos);
				ag->desiredSpeed = dtVlen(ag->targetPos);
			}
			else
			{
				if (ag->params.updateFlags & DT_CROWD_ANTICIPATE_TURNS)
					calcSmoothSteerDirection(ag, dvel);
				else
					calcStraightSteerDirection(ag, dvel);
				const float slowDownRadius = ag->params.radius * 2;
				const float speedScale = getDistanceToGoal(ag, slowDownRadius) / slowDownRadius;
				ag->desiredSpeed = ag->params.maxSpeed;
				dtVscale(dvel, dvel, ag->desiredSpeed * speedScale);
			}
			if (ag->params.updateFlags & DT_CROWD_SEPARATION)
			{
				const float separationDist = ag->params.collisionQueryRange;
				const float invSeparationDist = 1.0f / separationDist;
				float w = 0;
				float disp[3] = { 0, 0, 0 };
				for (int j = 0; j < ag->nneis; ++j)
				{
					const dtCrowdAgent* nei = &m_agents[ag->neis[j].idx];
					float diff[3];
					dtVsub(diff, ag->npos, nei->npos);
					diff[1] = 0;
					const float distSqr = dtVlenSqr(diff);
					if (distSqr < 0.00001f || distSqr > dtSqr(separationDist))
						continue;
					const float dist = dtMathSqrtf(distSqr);
					const float weight = ag->params.separationWeight * (1.0f - dtSqr(dist * invSeparationDist));
					dtVmad(disp, disp, diff, weight / dist);
					w += 1.0f;
				}
				if (w > 0.0001f)
				{
					dtVmad(dvel, dvel, disp, 1.0f / w);
					// No faster than the desired speed.
					const float speedSqr = dtVlenSqr(dvel);
					const float desiredSqr = dtSqr(ag->desiredSpeed);
					if (speedSqr > desiredSqr)
						dtVscale(dvel, dvel, desiredSqr / speedSqr);
				}
			}
			dtVcopy(ag->dvel, dvel);
		}
	};
	runPass(nagents, steer);
	m_passMilliseconds[DT_CROWD_PASS_STEERING] = clock.lap();

	// New velocities: sampled around neighbours and walls, or the desired one.
	for (int i = 0; i < m_workerCount; ++i)
		m_workerSamples[i] = 0;
	auto sampleVelocities = [&](const int begin, const int end, const int worker) {
		dtObstacleAvoidanceQuery* avoidance = worker < 0 ? m_obstacleQuery : m_workerAvoidance[worker];
		int samples = 0;
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING)
				continue;
			if (!(ag->params.updateFlags & DT_CROWD_OBSTACLE_AVOIDANCE))
			{
				dtVcopy(ag->nvel, ag->dvel);
				continue;
			}
			avoidance->reset();
			for (int j = 0; j < ag->nneis; ++j)
			{
				const dtCrowdAgent* nei = &m_agents[ag->neis[j].idx];
				avoidance->addCircle(nei->npos, nei->params.radius, nei->vel, nei->dvel);
			}
			for (int j = 0; j < ag->boundary.getSegmentCount(); ++j)
			{
				const float* s = ag->boundary.getSegment(j);
				// Walls the member stands behind are ignored.
				if (dtTriArea2D(ag->npos, s, s + 3) < 0.0f)
					continue;
				avoidance->addSegment(s, s + 3);
			}
			dtObstacleAvoidanceDebugData* vod = debugIdx == i ? debug->vod : 0;
			samples += avoidance->sampleVelocityAdaptive(ag->npos, ag->params.radius, ag->desiredSpeed, ag->vel, ag->dvel, ag->nvel,
														 &m_obstacleQueryParams[ag->params.obstacleAvoidanceType], vod);
		}
		if (worker < 0)
			m_velocitySampleCount += samples;
		else
			m_workerSamples[worker] += samples;
	};
	runPass(nagents, sampleVelocities);
	for (int i = 0; i < m_workerCount; ++i)
		m_velocitySampleCount += m_workerSamples[i];
	m_passMilliseconds[DT_CROWD_PASS_AVOIDANCE] = clock.lap();

	auto integrateAll = [&](const int begin, const int end, const int) {
		for (int i = begin; i < end; ++i)
		{
			if (agents[i]->state == DT_CROWDAGENT_STATE_WALKING)
				integrate(agents[i], dt);
		}
	};
	runPass(nagents, integrateAll);

	// Overlapping members push each other apart over four rounds; every round first measures all
	// pushes, then applies them.
	static const float COLLISION_RESOLVE_FACTOR = 0.7f;
	auto measurePush = [&](const int begin, const int end, const int) {
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING)
				continue;
			const int idx0 = getAgentIndex(ag);
			dtVset(ag->disp, 0, 0, 0);
			float w = 0;
			for (int j = 0; j < ag->nneis; ++j)
			{
				const dtCrowdAgent* nei = &m_agents[ag->neis[j].idx];
				const int idx1 = getAgentIndex(nei);
				float diff[3];
				dtVsub(diff, ag->npos, nei->npos);
				diff[1] = 0;
				float dist = dtVlenSqr(diff);
				if (dist > dtSqr(ag->params.radius + nei->params.radius))
					continue;
				dist = dtMathSqrtf(dist);
				float pen = (ag->params.radius + nei->params.radius) - dist;
				if (dist < 0.0001f)
				{
					// On top of each other: the two pick opposite sideways directions.
					if (idx0 > idx1)
						dtVset(diff, -ag->dvel[2], 0, ag->dvel[0]);
					else
						dtVset(diff, ag->dvel[2], 0, -ag->dvel[0]);
					pen = 0.01f;
				}
				else
				{
					pen = (1.0f / dist) * (pen * 0.5f) * COLLISION_RESOLVE_FACTOR;
				}
				dtVmad(ag->disp, ag->disp, diff, pen);
				w += 1.0f;
			}
			if (w > 0.0001f)
				dtVscale(ag->disp, ag->disp, 1.0f / w);
		}
	};
	for (int round = 0; round < 4; ++round)
	{
		runPass(nagents, measurePush);
		for (int i = 0; i < nagents; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state == DT_CROWDAGENT_STATE_WALKING)
				dtVadd(ag->npos, ag->npos, ag->disp);
		}
	}

	// Members move along the surface to their new positions.
	auto moveAlongSurface = [&](const int begin, const int end, const int worker) {
		dtNavMeshQuery* query = queryOf(worker);
		for (int i = begin; i < end; ++i)
		{
			dtCrowdAgent* ag = agents[i];
			if (ag->state != DT_CROWDAGENT_STATE_WALKING)
				continue;
			ag->corridor.movePosition(ag->npos, query, &m_filters[ag->params.queryFilterType]);
			dtVcopy(ag->npos, ag->corridor.getPos());
			// Without a path the corridor keeps only the polygon under the member.
			if (ag->targetState == DT_CROWDAGENT_TARGET_NONE || ag->targetState == DT_CROWDAGENT_TARGET_VELOCITY)
			{
				ag->corridor.reset(ag->corridor.getFirstPoly(), ag->npos);
				ag->partial = false;
			}
		}
	};
	runPass(nagents, moveAlongSurface);

	// Members crossing off-mesh connections: eased onto the start, then across.
	for (int i = 0; i < nagents; ++i)
	{
		dtCrowdAgent* ag = agents[i];
		dtCrowdAgentAnimation* anim = &m_agentAnims[getAgentIndex(ag)];
		if (!anim->active)
			continue;
		anim->t += dt;
		if (anim->t > anim->tmax)
		{
			anim->active = false;
			ag->state = DT_CROWDAGENT_STATE_WALKING;
			continue;
		}
		const float ta = anim->tmax * 0.15f;
		const float tb = anim->tmax;
		if (anim->t < ta)
			dtVlerp(ag->npos, anim->initPos, anim->startPos, tween(anim->t, 0.0, ta));
		else
			dtVlerp(ag->npos, anim->startPos, anim->endPos, tween(anim->t, ta, tb));
		dtVset(ag->vel, 0, 0, 0);
		dtVset(ag->dvel, 0, 0, 0);
	}
	m_passMilliseconds[DT_CROWD_PASS_MOVE] = clock.lap();
}
