# ComputeLab charter

## Purpose

ComputeLab is a standalone general-purpose compute-infrastructure
experiment for evaluating low-level GPU and systems technologies.

## Publication boundary

The repository must remain independently understandable and publishable.

## Hard separation

ComputeLab must not reproduce, depend on, expose, or anticipate the
architecture of any production application.

Only measurements, compatibility observations, and engineering
conclusions may cross from ComputeLab into another project.

## Allowed concepts

- arrays
- buffers
- indices
- points
- counters
- work items
- transfers
- synchronization
- contention
- generic numeric transforms
- generic visualization data

## Forbidden concepts

- neural entities
- synapses
- biological timing
- learning/plasticity
- production runtime semantics
- production model schemas
- production checkpoint formats
- production scheduler architecture
- production-compatible serialization
- domain-specific graph simulations disguised with generic names

## Experiment rule

If answering a question requires reproducing production semantics,
defer that question to production-integrated experimentation.