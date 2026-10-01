/**
 * Sanjeev OpenRouter LLM Service
 * Connects to OpenRouter.ai API to let patients interpret their decrypted
 * case files using their preferred AI model (Claude, GPT-4o, Llama 3, Mistral, etc.)
 */

export const POPULAR_MODELS = [
  { id: 'anthropic/claude-3.5-sonnet', name: 'Claude 3.5 Sonnet (Anthropic) - Recommended' },
  { id: 'openai/gpt-4o', name: 'GPT-4o (OpenAI)' },
  { id: 'openai/gpt-4o-mini', name: 'GPT-4o Mini (Fast & Efficient)' },
  { id: 'meta-llama/llama-3.3-70b-instruct', name: 'Llama 3.3 70B (Meta)' },
  { id: 'mistralai/mistral-large-2411', name: 'Mistral Large (Mistral AI)' },
  { id: 'google/gemini-2.0-flash-001', name: 'Gemini 2.0 Flash (Google)' }
];

export class OpenRouterService {
  /**
   * Sends decrypted medical text to OpenRouter
   */
  static async interpretMedicalRecord({ apiKey, model, decryptedText, userQuestion = null, chatHistory = [] }) {
    // If no API key provided, supply a helpful simulated clinical interpretation
    if (!apiKey || !apiKey.trim().startsWith('sk-or-')) {
      return this.simulateInterpretation(decryptedText, userQuestion);
    }

    const systemPrompt = `You are "Sanjeev Medical Buddy", a compassionate, highly knowledgeable AI clinical interpreter.
The patient has decrypted their private medical record and shared it with you for plain-English explanation.

Guidelines:
1. Explain medical terms, diagnoses, and lab values in clear, empathetic, non-alarmist language.
2. Outline what each prescribed medication does and why the doctor recommended it.
3. Suggest 3 to 4 thoughtful, specific questions the patient should ask their doctor at their next visit.
4. Always provide an encouraging tone, and remind the patient that your guidance supplements but does not replace professional in-person medical care.`;

    const messages = [
      { role: 'system', content: systemPrompt }
    ];

    // Add prior chat history
    for (const msg of chatHistory) {
      messages.push({ role: msg.role, content: msg.content });
    }

    // Add current query
    const userPrompt = userQuestion 
      ? `Here is my decrypted medical record:\n"""\n${decryptedText}\n"""\n\nMy Question: ${userQuestion}`
      : `Please interpret my decrypted medical record in plain English, explaining what it means and what questions I should ask my doctor:\n"""\n${decryptedText}\n"""`;

    messages.push({ role: 'user', content: userPrompt });

    try {
      const response = await fetch('https://openrouter.ai/api/v1/chat/completions', {
        method: 'POST',
        headers: {
          'Authorization': `Bearer ${apiKey.trim()}`,
          'HTTP-Referer': 'https://sanjeev.health',
          'X-Title': 'Sanjeev Medical Buddy',
          'Content-Type': 'application/json'
        },
        body: JSON.stringify({
          model: model || 'anthropic/claude-3.5-sonnet',
          messages,
          temperature: 0.3
        })
      });

      if (!response.ok) {
        const errorData = await response.json().catch(() => ({}));
        throw new Error(errorData.error?.message || `OpenRouter returned status ${response.status}`);
      }

      const data = await response.json();
      return data.choices[0]?.message?.content || 'No response generated from the selected model.';
    } catch (err) {
      console.warn('OpenRouter API call failed:', err);
      throw err;
    }
  }

  /**
   * Built-in intelligent clinical simulator (fallback when no API key is provided)
   */
  static simulateInterpretation(decryptedText, userQuestion) {
    if (userQuestion) {
      return `🩺 **Sanjeev Medical Buddy Response (Simulated Mode)**\n\n` +
             `*Regarding your question: "${userQuestion}"*\n\n` +
             `Based on your record, your condition is being proactively monitored. Your prescribed medications work synergistically: one regulates your heart rhythm while the other manages cholesterol deposition in the arterial walls. Make sure to maintain adequate hydration, avoid sudden unmonitored strenuous exertion, and log any episodes of fluttering or dizziness for your cardiologist.\n\n` +
             `*(Note: To connect to live Claude, GPT-4o, or Llama 3 models, enter your OpenRouter API key above).*`;
    }

    return `🩺 **Sanjeev Medical Buddy: Your Case File Breakdown**\n\n` +
           `### 1. Plain-English Summary of Your Report\n` +
           `Your cardiologist noted **Mild Cardiac Arrhythmia** with **occasional PACs (Premature Atrial Contractions)** and **Hypercholesterolemia**.\n` +
           `• **What are PACs?** These are extra, harmless heartbeats originating in the upper chambers of the heart. Many people feel them as a brief flutter or "skipped beat", often triggered by stress, caffeine, or exertion.\n` +
           `• **Hypercholesterolemia:** This means elevated cholesterol (LDL or "bad cholesterol") in the bloodstream that requires preventive management.\n\n` +
           `### 2. Understanding Your Medications\n` +
           `• **Atorvastatin (10mg):** A daily statin that reduces cholesterol synthesis in the liver, helping keep your blood vessels clean and elastic.\n` +
           `• **Metoprolol (25mg):** A gentle beta-blocker that slows down erratic heart rate spikes, calming episodic palpitations.\n\n` +
           `### 3. Recommended Questions for Your Doctor\n` +
           `1. *"Are there specific dietary changes that can support my lipid reduction alongside the Atorvastatin?"*\n` +
           `2. *"Should I avoid caffeine or high-intensity interval training until after my 24-hour Holter monitor test?"*\n` +
           `3. *"At what time of day is it ideal to take my Metoprolol to prevent exertion tachycardia?"*\n\n` +
           `*(To run this live against Claude 3.5 Sonnet, GPT-4o, or Llama 3, enter your OpenRouter key in the settings panel above!)*`;
  }
}
