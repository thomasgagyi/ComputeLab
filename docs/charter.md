# ComputeLab charter

## Purpose

ComputeLab is a standalone general-purpose compute-infrastructure experiment
for evaluating low-level GPU and systems technologies.

Its purpose is to produce engineering evidence about infrastructure candidates,
not to prototype any consuming application's architecture.

## Publication boundary

The repository must remain independently understandable and publishable.

A reader with access only to ComputeLab should not be able to infer the domain
architecture, semantics, data model, or implementation strategy of another
project from its source code or documentation.

## Hard separation

ComputeLab must not reproduce, depend on, expose, anticipate, or intentionally
mirror the architecture of any production application.

Production applications must not depend on ComputeLab source, libraries,
headers, APIs, schemas, serialized formats, build helpers, or architecture.

Only measurements, compatibility observations, experimental limitations, and
engineering conclusions may cross from ComputeLab into another project.

## Allowed concepts

ComputeLab may use ordinary infrastructure concepts such as:

- arrays
- buffers
- indices
- points
- counters
- work items
- numeric transforms
- transfers
- synchronization
- contention
- memory accesses
- dispatches
- generic visualization data

These concepts must remain general-purpose and must exist because the experiment
requires them.

## Forbidden concepts

ComputeLab must not contain:

- neural or biological entities
- domain-specific model objects
- learning or plasticity mechanisms
- biological or production-runtime timing semantics
- production scheduler architecture
- production checkpoint semantics or formats
- production-compatible persistence schemas
- production execution-image structures
- application-specific graph simulations
- generic names used merely to disguise production-domain structures

## Structural non-isomorphism

Renaming a production concept does not make it generic.

For example, replacing a production entity name with `Node`, `Record`, or
`WorkItem` is still forbidden when the structure or behavior intentionally
reproduces the production architecture.

ComputeLab experiments must instead be designed directly from general systems
questions.

## Experiment rule

If answering a question would require reproducing production semantics,
production structures, realistic production workloads, or production runtime
behavior, that question must be deferred to production-integrated
experimentation.

## Evidence scope

ComputeLab evidence may qualify an infrastructure candidate or support a
provisional engineering choice.

It cannot establish the final architecture or performance of a production
application whose workload is not represented here.

Later production evidence supersedes ComputeLab evidence when the two disagree.