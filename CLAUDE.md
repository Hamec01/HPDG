# HPDG — agent notes

Generation-quality and Sample Analysis work follows two documents:

- `docs/RULES.md` — mandatory rules (measure before tuning, same test before/after, quality > novelty,
  confidence must survive the pipeline, numbers not impressions, ...).
- `docs/ROADMAP_UPDATE.md` — the quality program and its stage order
  (measurement infrastructure first, then Boom Bap, Trap, DnB, Techno, then analysis).

Genre engines are separate (Boom Bap, Trap, DnB, Techno); a change to one must not change another
genre's generation. Third-party sample kits stay out of git unless the maintainer bundles them.
