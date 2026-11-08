-- Beaumont Family TV: one row per person, holding their latest location.
-- Run in the Supabase SQL editor. Then create one user per family member
-- (Authentication > Users) and disable public sign-ups
-- (Authentication > Providers > Email > turn off "Allow new users to sign up").
--
-- Phones sign in as themselves and may only write their own row.
-- The ESP32 reads every row with the service-role key (bypasses RLS), so no
-- read policy for other people is needed - and none is granted.

create table if not exists public.locations (
  user_id            uuid primary key references auth.users (id) on delete cascade,
  person             text not null,
  lat                double precision,
  lng                double precision,
  heading            double precision,   -- degrees clockwise from north; null if unknown/stationary
  speed_mps          double precision,
  zone               text,               -- e.g. 'Home', 'School'; null = on the way
  eta_home_minutes   integer,            -- 0 at home; null if unknown
  updated_at         timestamptz not null default now()
);

alter table public.locations enable row level security;

create policy "own row: read"   on public.locations for select using (auth.uid() = user_id);
create policy "own row: insert" on public.locations for insert with check (auth.uid() = user_id);
create policy "own row: update" on public.locations for update
  using (auth.uid() = user_id) with check (auth.uid() = user_id);
create policy "own row: delete" on public.locations for delete using (auth.uid() = user_id);

-- The TV page signs in as a dedicated read-only "tv" user and reads everyone's
-- row. Create the user (Authentication > Users; email + password, auto-confirm),
-- then replace the UUID below with its id and run this:
--
-- create policy "tv: read all" on public.locations for select
--   using (auth.uid() = '00000000-0000-0000-0000-000000000000');
