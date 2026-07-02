#ifndef AUDIO_ALERT_H
#define AUDIO_ALERT_H

namespace AudioAlert {

void PlayFatigueWarningAsync();
void PlayRecognitionCompleteAsync();
void PlayEnrollSuccessAsync();
void PlayCheckinSuccessAsync();
void PlayCheckoutSuccessAsync();

} // namespace AudioAlert

#endif // AUDIO_ALERT_H
