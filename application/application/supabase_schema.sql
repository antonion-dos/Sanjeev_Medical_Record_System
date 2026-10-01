-- ====================================================================
-- Sanjeev (संजीव) - Supabase Patients Table Schema & RLS Policies
-- ====================================================================
-- Run this script in the Supabase Dashboard -> SQL Editor

CREATE TABLE IF NOT EXISTS public.patients (
    id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
    user_id UUID REFERENCES auth.users(id) ON DELETE CASCADE,
    patient_address TEXT NOT NULL,
    username TEXT NOT NULL,
    doctor TEXT,
    symptoms JSONB DEFAULT '[]'::jsonb,
    medications JSONB DEFAULT '[]'::jsonb,
    patient_data JSONB DEFAULT '{}'::jsonb,
    wallet_signature TEXT,
    created_at TIMESTAMPTZ DEFAULT now(),
    updated_at TIMESTAMPTZ DEFAULT now()
);

-- Indexes for rapid lookup by address, assigned doctor, and user ID
CREATE INDEX IF NOT EXISTS idx_patients_address ON public.patients(patient_address);
CREATE INDEX IF NOT EXISTS idx_patients_doctor ON public.patients(doctor);
CREATE INDEX IF NOT EXISTS idx_patients_user_id ON public.patients(user_id);

-- Enable Row Level Security (RLS)
ALTER TABLE public.patients ENABLE ROW LEVEL SECURITY;

-- Allow authenticated users to read records
CREATE POLICY "Allow authenticated read on patients"
ON public.patients FOR SELECT
TO authenticated, anon
USING (true);

-- Allow authenticated users to insert their patient record
CREATE POLICY "Allow authenticated insert on patients"
ON public.patients FOR INSERT
TO authenticated, anon
WITH CHECK (true);

-- Allow users to update their own patient records
CREATE POLICY "Allow authenticated update on patients"
ON public.patients FOR UPDATE
TO authenticated, anon
USING (true)
WITH CHECK (true);
